#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""bench_vs_ninja.py — честный бенчмарк pmake vs ninja."""
import sys as _sys
try:
    _sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    _sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import argparse
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

IS_WINDOWS = platform.system() == "Windows"

def find_exe(name_or_path: str) -> str:
    p = Path(name_or_path)
    if p.exists() and p.is_file():
        return str(p.absolute())
    w = shutil.which(name_or_path)
    if not w:
        raise SystemExit(f"not found in PATH: {name_or_path}")
    return w

def gen_project(root: Path, n_files: int):
    src = root / "src"
    obj = root / "obj"
    out = root / "out"
    src.mkdir(parents=True, exist_ok=True)
    obj.mkdir(exist_ok=True)
    out.mkdir(exist_ok=True)

    for i in range(n_files):
        (src / f"file_{i}.c").write_text(f"int v{i};\n", encoding="utf-8")

    if IS_WINDOWS:
        # Pmake лексер превратит \" в ". 
        # В итоге command будет: cmd /c "echo. > obj/file_0.o"
        cmd_pmake = r'cmd /c \"echo. > ${out}\"'
        # Ninja напрямую передаст кавычки
        cmd_ninja = r'cmd /c "echo. > $out"'
    else:
        cmd_pmake = "touch ${out}"
        cmd_ninja = "touch $out"

    # --- build.pmake ---
    lines = [
        "pmake_required_version = 1.0",
        'workdir = "."',  # <-- КРИТИЧЕСКИЙ ФИКС: не менять рабочую папку на obj!
        f'rule cc {{ command = "{cmd_pmake}" }}',
        f'rule link {{ command = "{cmd_pmake}" }}',
    ]
    for i in range(n_files):
        lines.append(f'build "obj/file_{i}.o" : cc "src/file_{i}.c"')
    
    objs = " ".join(f'"obj/file_{i}.o"' for i in range(n_files))
    lines.append(f'build "out/app" : link {objs}')
    lines.append('alias all : "out/app"')
    (root / "build.pmake").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # --- build.ninja ---
    nl = [
        "rule cc",
        f"  command = {cmd_ninja}",
        "rule link",
        f"  command = {cmd_ninja}",
        "",
    ]
    for i in range(n_files):
        nl.append(f"build obj/file_{i}.o: cc src/file_{i}.c")
    
    objs_n = " ".join(f"obj/file_{i}.o" for i in range(n_files))
    nl.append(f"build out/app: link {objs_n}")
    nl.append("default out/app")
    (root / "build.ninja").write_text("\n".join(nl) + "\n", encoding="utf-8")

def clean_outputs(root: Path):
    for d in ("obj", "out"):
        p = root / d
        if p.exists():
            shutil.rmtree(p, ignore_errors=True)
        p.mkdir(exist_ok=True)
    for f in ("Project-cache.pmake", ".ninja_deps", ".ninja_log"):
        p = root / f
        if p.exists():
            try: p.unlink()
            except OSError: pass

def verify_outputs(root: Path, n_files: int) -> list[str]:
    missing = []
    for i in range(n_files):
        if not (root / "obj" / f"file_{i}.o").exists():
            missing.append(f"obj/file_{i}.o")
    if not (root / "out" / "app").exists():
        missing.append("out/app")
    return missing

def run(exe: str, args: list[str], cwd: Path, timeout: int = 300):
    t0 = time.perf_counter()
    try:
        p = subprocess.run(
            [exe] + args, cwd=str(cwd), timeout=timeout,
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        return time.perf_counter() - t0, p.returncode, p.stderr.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return time.perf_counter() - t0, -999, "TIMEOUT"

def bench(fn, runs: int, warmup: int = 1) -> dict:
    for _ in range(warmup):
        dt, rc, err = fn()
        if rc != 0: raise SystemExit(f"warmup failed: {err[:200]}")
    times = []
    for _ in range(runs):
        dt, rc, err = fn()
        if rc != 0: raise SystemExit(f"failed: {err[:200]}")
        times.append(dt)
    return {"median": statistics.median(times)}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", type=int, default=400)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--pmake", default="pmake")
    ap.add_argument("--ninja", default="ninja")
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    pmake_exe = find_exe(args.pmake)
    ninja_exe = find_exe(args.ninja)

    root = Path(tempfile.mkdtemp(prefix="pmake_bench_"))
    print(f"Project:  {root}")
    print(f"files:    {args.files}")
    print(f"jobs:     {args.jobs}")
    print(f"runs:     {args.runs} (+ 1 warmup)\n")

    gen_project(root, args.files)
    J = str(args.jobs)

    print("Initial build...")
    dt, rc, err = run(pmake_exe, ["-f", "build.pmake", "-j", J], root)
    if rc != 0: raise SystemExit(f"pmake initial failed: {err}")
    print(f"  exe 1: {dt*1000:.0f}ms")
    
    missing = verify_outputs(root, args.files)
    if missing:
        raise SystemExit(f"pmake initial MISSING outputs: {missing[:5]}")
    
    dt, rc, err = run(ninja_exe, ["-f", "build.ninja", "-j", J], root)
    if rc != 0: raise SystemExit(f"ninja initial failed: {err}")
    print(f"  exe 2: {dt*1000:.0f}ms")
    
    print(f"workdir: {root}")
    
    missing = verify_outputs(root, args.files)
    if missing:
        raise SystemExit(f"ninja initial MISSING outputs: {missing[:5]}")

    print("Benchmark: noop...")
    r_noop_p = bench(lambda: run(pmake_exe, ["-f", "build.pmake", "-j", J], root), args.runs)
    r_noop_n = bench(lambda: run(ninja_exe, ["-f", "build.ninja", "-j", J], root), args.runs)

    print("Benchmark: parse (-n)...")
    r_parse_p = bench(lambda: run(pmake_exe, ["-n", "-f", "build.pmake"], root), args.runs)
    r_parse_n = bench(lambda: run(ninja_exe, ["-n", "-f", "build.ninja"], root), args.runs)

    print("Benchmark: incremental...")
    def incr_p():
        p = root / "src" / "file_0.c"
        os.utime(p, (p.stat().st_atime, p.stat().st_mtime + 2.0))
        return run(pmake_exe, ["-f", "build.pmake", "-j", J], root)
    def incr_n():
        p = root / "src" / "file_0.c"
        os.utime(p, (p.stat().st_atime, p.stat().st_mtime + 2.0))
        return run(ninja_exe, ["-f", "build.ninja", "-j", J], root)
    r_incr_p = bench(incr_p, args.runs)
    r_incr_n = bench(incr_n, args.runs)

    print("Benchmark: full rebuild...")
    rebuild_runs = max(2, args.runs // 2)
    def rebuild_p():
        clean_outputs(root)
        dt, rc, err = run(pmake_exe, ["-f", "build.pmake", "-j", J], root)
        if rc == 0:
            missing = verify_outputs(root, args.files)
            if missing: raise SystemExit(f"pmake rebuild MISSING outputs: {missing[:5]}")
        return dt, rc, err
    def rebuild_n():
        clean_outputs(root)
        dt, rc, err = run(ninja_exe, ["-f", "build.ninja", "-j", J], root)
        if rc == 0:
            missing = verify_outputs(root, args.files)
            if missing: raise SystemExit(f"ninja rebuild MISSING outputs: {missing[:5]}")
        return dt, rc, err
    r_rebuild_p = bench(rebuild_p, rebuild_runs, warmup=1)
    r_rebuild_n = bench(rebuild_n, rebuild_runs, warmup=1)

    results = {
        "noop": {"pmake": r_noop_p, "ninja": r_noop_n},
        "parse": {"pmake": r_parse_p, "ninja": r_parse_n},
        "incremental": {"pmake": r_incr_p, "ninja": r_incr_n},
        "rebuild": {"pmake": r_rebuild_p, "ninja": r_rebuild_n},
    }

    print("\n" + "=" * 76)
    print(f" RESULTS — {args.files} files, -j{args.jobs}")
    print("=" * 76)
    print(f" {'benchmark':<14} {'exe 1':>12} {'exe 2':>12} {'ratio':>10}  {'verdict'}")
    print("-" * 76)
    for name in ("noop", "parse", "incremental", "rebuild"):
        p = results[name]["pmake"]["median"] * 1000
        n = results[name]["ninja"]["median"] * 1000
        ratio = p / n if n > 0 else float("inf")
        verdict = "exe 1 faster" if ratio < 0.9 else "exe 1 SLOWER" if ratio > 1.15 else "~equal"
        print(f" {name:<14} {p:>9.1f}ms {n:>9.1f}ms {ratio:>9.2f}x  {verdict}")
    print("=" * 76)

    if not args.keep:
        shutil.rmtree(root, ignore_errors=True)
        print(f"\n Cleaned: {root}")

if __name__ == "__main__":
    main()