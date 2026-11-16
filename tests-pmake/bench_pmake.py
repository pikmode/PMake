#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bench_pmake.py — измеряет скорость pmake на N синтетических builds.

Использование:
    python bench_pmake.py --pmake path\\to\\pmake.exe -n 1000
    python bench_pmake.py --pmake path\\to\\pmake.exe -n 1000 --with-depfiles
    python bench_pmake.py --pmake path\\to\\pmake.exe -n 10000 --jobs 2

Что тестирует:
    [1/4] Холодный dry-run: парсер + граф + SHA1 + stat
    [2/4] Создание всех output-файлов вручную (+ .d файлов если --with-depfiles)
    [3/4] NOOP dry-run — КЛЮЧЕВАЯ МЕТРИКА
    [4/4] Инкремент (изменён 1 source из N) — ожидаем 1 задачу

Флаг --with-depfiles:
    25% build-ов получают depfile с N header'ами.
    Триггерит issue #1 (depfile-stat bottleneck) на noop.
    Без фикса collectDirtyNodes: noop взорвётся в 10-30 раз.

ВНИМАНИЕ: на Core 2 Duo:
    N=1000   — ~1-3 мин
    N=10000  — ~5-15 мин
    N=100000 — 30 мин - 2 часа
"""
from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


# ------------------------------------------------------------------ utils

def find_pmake(hint: str | None) -> Path:
    if hint:
        p = Path(hint)
        if p.exists():
            return p.absolute()
        w = shutil.which(hint)
        if w:
            return Path(w).absolute()
        raise SystemExit(f"pmake not found: {hint}")
    for name in ("pmake", "pmake.exe"):
        w = shutil.which(name)
        if w:
            return Path(w).absolute()
    raise SystemExit("pmake.exe not found in PATH. Use --pmake PATH")


def fmt_time(sec: float) -> str:
    if sec < 1.0:
        return f"{sec * 1000:.0f} ms"
    if sec < 60.0:
        return f"{sec:.2f} s"
    return f"{sec / 60:.1f} min"


def default_jobs() -> int:
    try:
        return max(1, os.cpu_count() or 1)
    except Exception:
        return 2


def default_command() -> str:
    if platform.system() == "Windows":
        return "cmd /c type nul > ${out}"
    return "sh -c ': > ${out}'"


# ------------------------------------------------------------- generate

def generate_project(root: Path, n: int, command: str,
                     with_depfiles: bool = False,
                     depfile_hdrs: int = 100) -> None:
    """Пишет build.pmake и создаёт N source-файлов (+ headers если depfiles)."""
    src_dir = root / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    parts = [
        "pmake_required_version = 1.0\n",
        "rule mk {\n",
        f'    command = "{command}"\n',
        "}\n\n",
    ]
    if with_depfiles:
        parts.append("rule mk_dep {\n")
        parts.append(f'    command = "{command}"\n')
        parts.append('    depfile = "${out}.d"\n')
        parts.append('    deps = "gcc"\n')
        parts.append("}\n\n")

    for i in range(1, n + 1):
        if with_depfiles and i % 4 == 0:
            parts.append(f'build "obj/o_{i:06d}.txt" : mk_dep "src/s_{i:06d}.txt"\n')
        else:
            parts.append(f'build "obj/o_{i:06d}.txt" : mk "src/s_{i:06d}.txt"\n')

    parts.append("\n")
    parts.append("alias all : ")
    parts.append(", ".join(f'"obj/o_{i:06d}.txt"' for i in range(1, n + 1)))
    parts.append("\n")
    (root / "build.pmake").write_text("".join(parts), encoding="utf-8")

    # source-файлы
    for i in range(1, n + 1):
        (src_dir / f"s_{i:06d}.txt").touch()

    if with_depfiles:
        inc_dir = root / "include"
        inc_dir.mkdir(exist_ok=True)
        obj_dir = root / "obj"
        obj_dir.mkdir(exist_ok=True)

        hdrs = [f"include/hdr_{j:04d}.h" for j in range(depfile_hdrs)]
        for h in hdrs:
            (root / h).touch()
        hdr_str = " ".join(hdrs)

        for i in range(1, n + 1):
            if i % 4 != 0:
                continue
            dep = obj_dir / f"o_{i:06d}.txt.d"
            dep.write_text(
                f"obj/o_{i:06d}.txt: src/s_{i:06d}.txt {hdr_str}\n",
                encoding="utf-8")


def touch_outputs(root: Path, n: int) -> None:
    """Создаёт N output-файлов. mtime(out) > mtime(src) — это noop baseline."""
    obj_dir = root / "obj"
    obj_dir.mkdir(exist_ok=True)
    for i in range(1, n + 1):
        (obj_dir / f"o_{i:06d}.txt").touch()


# ------------------------------------------------------------------ run

def run_pmake(exe: Path, args: list[str], cwd: Path, timeout: int) -> dict:
    t0 = time.perf_counter()
    try:
        p = subprocess.run(
            [str(exe)] + args,
            cwd=str(cwd),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
        return {
            "exit": p.returncode,
            "stdout": p.stdout or "",
            "stderr": p.stderr or "",
            "elapsed": time.perf_counter() - t0,
            "timeout": False,
        }
    except subprocess.TimeoutExpired as e:
        def dec(x):
            if x is None:
                return ""
            return x.decode("utf-8", "replace") if isinstance(x, bytes) else x
        return {
            "exit": -999,
            "stdout": dec(e.stdout),
            "stderr": dec(e.stderr) + f"\n[TIMEOUT {timeout}s]",
            "elapsed": time.perf_counter() - t0,
            "timeout": True,
        }


def count_tasks(stdout: str) -> int:
    seen: set[tuple[int, int]] = set()
    for line in stdout.splitlines():
        s = line.strip()
        if not s.startswith("["):
            continue
        end = s.find("]")
        if end < 2:
            continue
        inner = s[1:end]
        if "/" not in inner:
            continue
        a, b = inner.split("/", 1)
        a, b = a.strip(), b.strip()
        if a.isdigit() and b.isdigit():
            seen.add((int(a), int(b)))
    return len(seen)


# ------------------------------------------------------------------ main

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Measure pmake performance on N synthetic builds.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--pmake", help="Path to pmake(.exe)")
    ap.add_argument("-n", "--num", type=int, default=1000,
                    help="Number of builds (default 1000)")
    ap.add_argument("--jobs", type=int, default=default_jobs(),
                    help=f"Jobs (-j). Default: {default_jobs()}")
    ap.add_argument("--command", default=default_command(),
                    help="Command for rule mk")
    ap.add_argument("--timeout", type=int, default=3600,
                    help="Per-run timeout in seconds (default 3600)")
    ap.add_argument("--keep", action="store_true",
                    help="Keep temp dir after run")
    ap.add_argument("--workdir", help="Use specific workdir instead of mkdtemp")
    ap.add_argument("--with-depfiles", action="store_true",
                    help="25%% builds get depfile (N headers) — triggers issue #1")
    ap.add_argument("--depfile-count", type=int, default=100,
                    help="Headers per depfile (default 100)")
    args = ap.parse_args()

    n = args.num

    # --- Предупреждения для слабых машин ---
    if n >= 100_000:
        print(
            f"[WARN] N={n}. На Core 2 Duo это займёт 30+ минут. "
            f"Настоятельно рекомендую сначала прогнать -n 1000 и -n 10000.",
            file=sys.stderr,
        )

    pmake = find_pmake(args.pmake)
    print(f"pmake: {pmake}")
    print(f"N    : {n}")
    print(f"-j   : {args.jobs}")
    print(f"cmd  : {args.command}")
    if args.with_depfiles:
        print(f"depfiles: ON (25% builds, {args.depfile_count} headers each)")
    else:
        print(f"depfiles: off")
    print()

    # --- workdir ---
    if args.workdir:
        work = Path(args.workdir).resolve()
        if work.exists():
            shutil.rmtree(work, ignore_errors=True)
        work.mkdir(parents=True)
    else:
        work = Path(tempfile.mkdtemp(prefix=f"pmake_bench_{n}_"))

    try:
        # --- Phase 0: generate ---
        print(f"[0/4] Генерация проекта...")
        t0 = time.perf_counter()
        generate_project(work, n, args.command,
                         with_depfiles=args.with_depfiles,
                         depfile_hdrs=args.depfile_count)
        gen_time = time.perf_counter() - t0
        total_files = sum(1 for _ in work.rglob("*") if _.is_file())
        total_size = sum(f.stat().st_size for f in work.rglob("*") if f.is_file())
        print(f"      {fmt_time(gen_time)}, files={total_files}, "
              f"size={total_size / 1024 / 1024:.1f} MB")
        print()

        results: list[tuple[str, float, int]] = []

        # --- Phase 1: cold dry-run ---
        print(f"[1/4] Cold dry-run ({n} tasks dirty)...")
        r = run_pmake(pmake, ["-f", "build.pmake", "-n", "-j", str(args.jobs),
                              "build", "all"], work, args.timeout)
        if r["exit"] != 0:
            print(f"      FAILED (exit {r['exit']})")
            print(f"      stderr: {r['stderr'][:800]}")
            return 1
        tc = count_tasks(r["stdout"])
        print(f"      {fmt_time(r['elapsed'])}   tasks={tc}")
        results.append(("cold dry-run", r["elapsed"], tc))
        print()

        # --- Phase 2: create outputs ---
        print(f"[2/4] Создание {n} output-файлов...")
        t0 = time.perf_counter()
        touch_outputs(work, n)
        print(f"      {fmt_time(time.perf_counter() - t0)}")
        print()

        # --- Phase 3: noop ---
        print(f"[3/4] NOOP dry-run (все outputs существуют, делать нечего)...")
        r = run_pmake(pmake, ["-f", "build.pmake", "-n", "-j", str(args.jobs),
                              "build", "all"], work, args.timeout)
        if r["exit"] != 0:
            print(f"      FAILED (exit {r['exit']})")
            print(f"      stderr: {r['stderr'][:800]}")
            return 1
        tc = count_tasks(r["stdout"])
        print(f"      {fmt_time(r['elapsed'])}   tasks={tc}")
        results.append(("noop", r["elapsed"], tc))
        if tc != 0:
            print(f"      [WARN] Ожидали 0 задач, получили {tc}")
        print()

        # --- Phase 4: incremental ---
        print(f"[4/4] Инкремент (touch 1 source из {n})...")
        time.sleep(0.1)
        (work / "src" / "s_000001.txt").touch()
        r = run_pmake(pmake, ["-f", "build.pmake", "-n", "-j", str(args.jobs),
                              "build", "all"], work, args.timeout)
        if r["exit"] != 0:
            print(f"      FAILED (exit {r['exit']})")
            print(f"      stderr: {r['stderr'][:800]}")
            return 1
        tc = count_tasks(r["stdout"])
        print(f"      {fmt_time(r['elapsed'])}   tasks={tc}")
        results.append(("incremental (1 file)", r["elapsed"], tc))
        if tc != 1:
            print(f"      [WARN] Ожидали 1 задачу, получили {tc}")
        print()

        # --- summary ---
        print("=" * 72)
        print(f" SUMMARY (N={n}, depfiles={'ON' if args.with_depfiles else 'off'})")
        print("=" * 72)
        for name, elapsed, tc in results:
            print(f"  {name:<25} {fmt_time(elapsed):>10}  tasks={tc}")
        print("=" * 72)

        # --- verdict ---
        print()
        print(" Анализ:")
        cold_time = results[0][1]
        noop_time = results[1][1]
        incr_time = results[2][1]
        noop_tasks = results[1][2]
        incr_tasks = results[2][2]

        per_node_us = (noop_time / n) * 1_000_000 if n > 0 else 0
        print(f"  noop на node: {per_node_us:.1f} µs")

        # Пороги для depfile-mode строже — если issue #1 не фиксен,
        # noop на 10K будет 5-15 сек (500-1500 µs/node).
        if args.with_depfiles:
            if noop_time > 5.0:
                print(f"  🔴 NOOP С DEPFILE ВЗОРВАН: {fmt_time(noop_time)}")
                print(f"     -> issue #1 активен: depfile-inputs не prefetch'ятся")
            elif noop_time > 1.5:
                print(f"  🟠 NOOP С DEPFILE МЕДЛЕННЫЙ: {fmt_time(noop_time)}")
            elif noop_time > 0.3:
                print(f"  🟡 NOOP С DEPFILE ПРИЕМЛЕМЫЙ: {fmt_time(noop_time)}")
            else:
                print(f"  ✅ NOOP С DEPFILE ОТЛИЧНЫЙ: {fmt_time(noop_time)} (issue #1 fixed)")
        else:
            if noop_time > 60.0:
                print(f"  🔴 NOOP КРИТИЧЕСКИ МЕДЛЕННЫЙ: {fmt_time(noop_time)}")
            elif noop_time > 15.0:
                print(f"  🟠 NOOP МЕДЛЕННЫЙ: {fmt_time(noop_time)}")
            elif noop_time > 3.0:
                print(f"  🟡 NOOP ПРИЕМЛЕМЫЙ: {fmt_time(noop_time)}")
            else:
                print(f"  ✅ NOOP ХОРОШИЙ: {fmt_time(noop_time)}")

        if noop_tasks != 0:
            print(f"  🔴 NOOP НЕ ПУСТОЙ: {noop_tasks} задач (ожидали 0)")

        if incr_tasks == 1:
            print(f"  ✅ ИНКРЕМЕНТ КОРРЕКТЕН: 1 задача")
        elif incr_tasks == 0:
            print(f"  🔴 ИНКРЕМЕНТ СЛОМАН: 0 задач")
        elif incr_tasks > 100:
            print(f"  🔴 ИНКРЕМЕНТ ВЗРЫВНОЙ: {incr_tasks} задач")
        else:
            print(f"  🟠 ИНКРЕМЕНТ НЕТОЧНЫЙ: {incr_tasks} задач (ожидали 1)")

        if cold_time > 0:
            ratio = noop_time / cold_time
            print(f"  cold/noop ratio: {ratio:.2f}  (норма 0.05-0.30)")

        return 0

    finally:
        if not args.keep and not args.workdir:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print(f"\nWorkdir сохранён: {work}")


if __name__ == "__main__":
    sys.exit(main())