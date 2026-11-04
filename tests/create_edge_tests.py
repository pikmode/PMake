# -*- coding: utf-8 -*-
"""create_edge_tests.py — edge cases order-only, generator, CMake (8 тестов).

ИСПРАВЛЕНО:
  real_order_only_and_implicit — copy.py → concat.py (copy2 наследует mtime)
  real_order_only_restat       — ожидание taskCount=1 при modify (order-only
                                 не propagates dirty)
  real_cmake_full_pattern      — ожидание taskCount=1 при touch gen.in
                                 (order-only не propagates)

Запуск:
    D:\\bin\\python3.exe create_edge_tests.py --dir C:\\Users\\abubakir\\Desktop\\TEST
    D:\\bin\\python3.exe run.py --pmake pmake.exe
"""
from __future__ import annotations
import sys as _sys
try:
    _sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    _sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import sys
import argparse
import json
import shutil
from pathlib import Path

DEFAULT_DIR = r"C:\Users\abubakir\Desktop\TEST"
REAL_PYTHON = "E:/python/python.exe"

HELPERS_EXTRA = {}
TESTS_EXTRA = []


def pp(s: str) -> str:
    return s.replace("@PYTHON@", REAL_PYTHON)


def test(name, files, runs, description="", bug="", timeout=60):
    TESTS_EXTRA.append({
        "name": name,
        "files": {k: pp(v) for k, v in files.items()},
        "meta": {
            "description": description,
            "bug": bug,
            "timeoutSec": timeout,
            "args": ["-f", "build.pmake", "-v"],
            "runs": runs,
        },
    })


# ============================================================
# ORDER-ONLY EDGE CASES
# ============================================================

# restat + order-only.
# СЕМАНТИКА: order-only не propagates dirty. Даже если stamp пересобран
# (mtime изменился), out НЕ пересобирается. taskCount=1 при modify.
# restat проверяется на самом stamp: touch (контент тот же) → skip;
# modify (контент другой) → rebuild.
test("real_order_only_restat", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_restat {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
rule check_stamp {
    command = "${python} check_stamp.py stamp.txt ${in} ${out}"
}
build "stamp.txt" : gen_restat "stamp.in"
build "out.txt" : check_stamp "out.in" || "stamp.txt"
alias all : "out.txt"
''',
    "stamp.in": "constant\n",
    "out.in": "out\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2,
                                 "files": ["stamp.txt", "out.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    # touch stamp.in: контент тот же → copy_preserve skip → mtime не изменился
    # → restat=2 → stamp тоже "skip-помечен", out skip. taskCount=1
    {"name": "touch_stamp_skip",
     "before": [{"sleep": 0.3}, {"touch": ["stamp.in"]}],
     "expect": {"exit": 0, "taskCount": 1}},
    # modify stamp.in: контент другой → stamp пересобран (mtime new)
    # НО out НЕ пересобирается (order-only не propagates). taskCount=1
    {"name": "modify_stamp_order_only_no_propagate",
     "before": [{"sleep": 0.3}, {"modify": {"stamp.in": "changed\n"}}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="order-only + restat: order-only не propagates dirty (taskCount=1)")


test("real_order_only_generator", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_once {
    command = "${python} touch.py ${out}"
    generator = true
}
rule check_stamp {
    command = "${python} check_stamp.py stamp.txt ${in} ${out}"
}
build "stamp.txt" : gen_once
build "out.txt" : check_stamp "out.in" || "stamp.txt"
alias all : "out.txt"
''',
    "out.in": "out\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2,
                                 "files": ["stamp.txt", "out.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "delete_stamp",
     "before": [{"sleep": 0.3}, {"delete": ["stamp.txt"]}],
     "expect": {"exit": 0, "taskCount": 1,
                "files": ["stamp.txt", "out.txt"]}},
], description="order-only + generator: delete stamp → stamp rebuild, out skip")


test("real_order_only_missing", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out.txt" : copy "in.txt" || "nonexistent.stamp"
alias all : "out.txt"
''',
    "in.txt": "in\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["out.txt"],
                                 "filesWith": {"out.txt": "in"}}},
], description="missing order-only: leaf игнорируется (ninja = error)")


test("real_order_only_multi", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule slow_touch {
    command = "${python} sleep_and_touch.py 300 ${out}"
}
rule check_two {
    command = "${python} check_two.py s1.txt s2.txt ${out}"
}
build "s1.txt" : slow_touch
build "s2.txt" : slow_touch
build "out.txt" : check_two || "s1.txt" "s2.txt"
alias all : "out.txt"
''',
}, runs=[
    {"name": "j2_ordered",
     "args": ["-f", "build.pmake", "-j", "2"],
     "expect": {"exit": 0, "taskCount": 3,
                "files": ["s1.txt", "s2.txt", "out.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.5}],
     "expect": {"exit": 0, "taskCount": 0}},
], description="multi order-only: out || s1 s2 — ждёт обоих")


# FIX: copy.py → concat.py. copy2 наследует mtime → hdr.txt мог быть
# новее out.txt (mtime от main.txt) → out dirty на каждом noop.
# concat.py делает write_text → mtime = now → out всегда новее hdr.
test("real_order_only_and_implicit", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_out { command = "${python} concat.py ${out} ${in}" }
rule gen_stamp { command = "${python} touch.py ${out}" }
build "stamp.txt" : gen_stamp
build "out.txt" : gen_out "main.txt" | "hdr.txt" || "stamp.txt"
alias all : "out.txt"
''',
    "main.txt": "main\n",
    "hdr.txt": "hdr\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2,
                                 "files": ["stamp.txt", "out.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    # touch implicit hdr → out пересобирается (implicit propagates)
    {"name": "touch_implicit",
     "before": [{"sleep": 0.3}, {"touch": ["hdr.txt"]}],
     "expect": {"exit": 0, "taskCount": 1}},
    # delete stamp → stamp rebuild, out НЕ пересобирается (order-only)
    {"name": "delete_stamp",
     "before": [{"sleep": 0.3}, {"delete": ["stamp.txt"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="implicit + order-only: | propagates, || не propagates")


# ============================================================
# GENERATOR MTIME (KNOWN BUG — фикс в node.cpp)
# ============================================================
test("real_generator_mtime", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen {
    command = "${python} touch.py ${out}"
    generator = true
}
build "gen.txt" : gen "src.in"
alias all : "gen.txt"
''',
    "src.in": "src\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["gen.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "touch_src_should_rebuild",
     "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="generator mtime: touch src → gen пересобирается (P0 fix)")
# ============================================================
# CMake FULL PATTERN
# ============================================================

# FIX: touch gen.in → autogen_stamp пересобран, main.o и app НЕ пересобираются
# (order-only через phony не propagates). taskCount=1 (только autogen).
# Но touch main.c → main.o пересобирается (обычный input), app тоже (был
# rebuild main.o). taskCount=2.
test("real_cmake_full_pattern", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule autogen {
    command = "${python} sleep_and_touch.py 200 ${out}"
    restat = true
}
rule compile {
    command = "${python} check_stamp.py autogen_stamp ${in} ${out}"
}
build "autogen_stamp" : autogen "gen.in"
build "cmake_obj_deps" : phony || "autogen_stamp"
build "main.o" : compile "main.c" || "cmake_obj_deps"
build "app" : compile "main.o" || "cmake_obj_deps"
alias all : "app"
''',
    "gen.in": "gen\n",
    "main.c": "main\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                 "files": ["autogen_stamp", "main.o", "app"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    # touch gen.in: autogen rebuild, main.o и app skip (order-only)
    {"name": "touch_gen_order_only_no_propagate",
     "before": [{"sleep": 0.3}, {"touch": ["gen.in"]}],
     "expect": {"exit": 0, "taskCount": 1}},
    # touch main.c: main.o rebuild (обычный input), app rebuild (main.o был rebuild)
    {"name": "touch_main_c_rebuild",
     "before": [{"sleep": 0.3}, {"touch": ["main.c"]}],
     "expect": {"exit": 0, "taskCount": 2}},
], description="CMake: autogen (order-only) не propagates; main.c → main.o + app")


test("real_pool_console_with_order", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule console_task {
    command = "${python} sleep_and_touch.py 300 ${out}"
    pool = console
}
build "stamp.txt" : console_task
build "out.txt" : console_task || "stamp.txt"
alias all : "out.txt"
''',
}, runs=[
    {"name": "j8_serialized",
     "args": ["-f", "build.pmake", "-j", "8"],
     "expect": {"exit": 0, "taskCount": 2,
                "files": ["stamp.txt", "out.txt"],
                "elapsedMin": 0.55}},
], description="pool=console + order-only: сериализация сохраняется")


# ============================================================
# RUNNER
# ============================================================
RUN_PY = r'''#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import sys
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import argparse
import fnmatch
import json
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
TESTS_DIR = HERE / "tests"
HELPERS_DIR = HERE / "helpers"
RESULTS_DIR = HERE / "results"


def find_pmake(hint):
    if hint:
        p = Path(hint)
        if p.exists(): return p.absolute()
        w = shutil.which(hint)
        if w: return Path(w).absolute()
        raise SystemExit(f"pmake not found: {hint}")
    w = shutil.which("pmake") or shutil.which("pmake.exe")
    if w: return Path(w).absolute()
    raise SystemExit("pmake.exe not found. Use --pmake PATH")


def run_pmake(exe, args, cwd, timeout):
    t0 = time.time()
    try:
        p = subprocess.run(
            [str(exe)] + list(args), cwd=str(cwd), timeout=timeout,
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        return {"exit": p.returncode, "stdout": p.stdout or "",
                "stderr": p.stderr or "", "elapsed": time.time() - t0,
                "timedOut": False}
    except subprocess.TimeoutExpired as e:
        def dec(x):
            if x is None: return ""
            if isinstance(x, bytes): return x.decode("utf-8", "replace")
            return x
        return {"exit": -999, "stdout": dec(e.stdout),
                "stderr": dec(e.stderr) + f"\n[TIMEOUT {timeout}s]",
                "elapsed": time.time() - t0, "timedOut": True}
    except Exception as e:
        return {"exit": -998, "stdout": "",
                "stderr": f"[runner] {type(e).__name__}: {e}",
                "elapsed": time.time() - t0, "timedOut": False}


def apply_actions(actions, cwd):
    for a in actions:
        if "sleep" in a: time.sleep(a["sleep"])
        elif "touch" in a:
            for path in a["touch"]:
                p = cwd / path
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
        elif "delete" in a:
            for path in a["delete"]:
                p = cwd / path
                if p.exists(): p.unlink()
        elif "modify" in a:
            for path, content in a["modify"].items():
                p = cwd / path
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(content.encode("utf-8"))


def parse_task_count(stdout):
    seen = set()
    for line in stdout.splitlines():
        s = line.strip()
        if not s.startswith("["): continue
        end = s.find("]")
        if end < 2: continue
        inner = s[1:end]
        if "/" not in inner: continue
        parts = inner.split("/")
        if len(parts) == 2 and parts[0].strip().isdigit() and parts[1].strip().isdigit():
            seen.add((int(parts[0]), int(parts[1])))
    return len(seen)


def check_expect(expect, result, cwd):
    fails = []
    if "exit" in expect and result["exit"] != expect["exit"]:
        fails.append(f"exit={result['exit']}, expected {expect['exit']}")
    if result.get("timedOut"):
        fails.append("TIMEOUT"); return fails
    tc = parse_task_count(result["stdout"])
    if "taskCount" in expect and tc != expect["taskCount"]:
        fails.append(f"taskCount={tc}, expected {expect['taskCount']}")
    if "taskCountMin" in expect and tc < expect["taskCountMin"]:
        fails.append(f"taskCount={tc} < min {expect['taskCountMin']}")
    if "taskCountMax" in expect and tc > expect["taskCountMax"]:
        fails.append(f"taskCount={tc} > max {expect['taskCountMax']}")
    if "elapsedMin" in expect and result["elapsed"] < expect["elapsedMin"]:
        fails.append(f"elapsed={result['elapsed']:.2f}s < min {expect['elapsedMin']}")
    if "elapsedMax" in expect and result["elapsed"] > expect["elapsedMax"]:
        fails.append(f"elapsed={result['elapsed']:.2f}s > max {expect['elapsedMax']}")
    for s in expect.get("stdoutHas", []):
        if s not in result["stdout"]: fails.append(f"stdout missing: {s!r}")
    for s in expect.get("stderrHas", []):
        if s not in result["stderr"]: fails.append(f"stderr missing: {s!r}")
    for pat in expect.get("stdoutMatches", []):
        if not re.search(pat, result["stdout"]):
            fails.append(f"stdout no match: {pat!r}")
    for pat in expect.get("stderrMatches", []):
        if not re.search(pat, result["stderr"]):
            fails.append(f"stderr no match: {pat!r}")
    for f in expect.get("files", []):
        if not (cwd / f).exists(): fails.append(f"missing file: {f}")
    for f in expect.get("filesNot", []):
        if (cwd / f).exists(): fails.append(f"unexpected file: {f}")
    for f, content in expect.get("filesWith", {}).items():
        p = cwd / f
        if not p.exists():
            fails.append(f"missing file: {f}"); continue
        try:
            actual = p.read_text(encoding="utf-8", errors="replace").strip()
            if actual != content.strip():
                fails.append(f"file {f}: expected {content!r}, got {actual[:200]!r}")
        except Exception as e:
            fails.append(f"file {f}: read error: {e}")
    return fails


def run_test(pmake, test_dir, helpers_dir, keep_temp):
    meta = json.loads((test_dir / "meta.json").read_text(encoding="utf-8"))
    name = test_dir.name
    temp_root = Path(tempfile.mkdtemp(prefix=f"pmake_{name}_"))
    work = temp_root / "work"
    work.mkdir()
    for f in test_dir.iterdir():
        if f.name == "meta.json": continue
        if f.is_file(): shutil.copy2(f, work / f.name)
        elif f.is_dir(): shutil.copytree(f, work / f.name)
    for h in helpers_dir.iterdir():
        if h.is_file(): shutil.copy2(h, work / h.name)

    timeout = int(meta.get("timeoutSec", 60))
    default_args = meta.get("args", ["-f", "build.pmake"])
    logs, all_fails, total_elapsed = [], [], 0.0

    for run in meta.get("runs", []):
        run_name = run.get("name", "run")
        try: apply_actions(run.get("before", []), work)
        except Exception as e:
            all_fails.append(f"[{run_name}] before: {type(e).__name__}: {e}")
            break
        args = run.get("args", default_args)
        result = run_pmake(pmake, args, work, timeout)
        total_elapsed += result["elapsed"]
        logs.append(f"=== {run_name} ===\n"
                    f"args: {args}\nexit: {result['exit']}\n"
                    f"elapsed: {result['elapsed']:.2f}s\n\n"
                    f"--- STDOUT ---\n{result['stdout']}\n"
                    f"--- STDERR ---\n{result['stderr']}\n")
        fails = check_expect(run.get("expect", {}), result, work)
        if fails:
            all_fails.extend([f"[{run_name}] {x}" for x in fails])
            if not run.get("continue", False): break

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    log_path = RESULTS_DIR / f"{name}.log"
    header = f"=== TEST: {name} ===\n"
    if meta.get("description"): header += f"Description: {meta['description']}\n"
    if meta.get("bug"): header += f"Known bug: {meta['bug']}\n"
    log_path.write_text(header + "\n" + "\n".join(logs), encoding="utf-8")
    if not keep_temp: shutil.rmtree(temp_root, ignore_errors=True)
    return {"name": name, "status": "PASS" if not all_fails else "FAIL",
            "failures": all_fails, "elapsed": total_elapsed,
            "meta": meta, "log": str(log_path)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pmake")
    ap.add_argument("--filter", default="*")
    ap.add_argument("--keep-temp", action="store_true")
    args = ap.parse_args()
    pmake = find_pmake(args.pmake)
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    for f in RESULTS_DIR.glob("*.log"): f.unlink()
    for f in RESULTS_DIR.glob("*.json"): f.unlink()
    test_dirs = sorted(d for d in TESTS_DIR.iterdir()
                       if d.is_dir() and (d / "meta.json").exists())
    if args.filter != "*":
        test_dirs = [d for d in test_dirs if fnmatch.fnmatch(d.name, args.filter)]
    print("=" * 72)
    print(f" PMAKE TEST SUITE ({len(test_dirs)} tests)")
    print(f" pmake: {pmake}")
    print("=" * 72); print()

    results = []
    for td in test_dirs:
        r = run_test(pmake, td, HELPERS_DIR, args.keep_temp)
        results.append(r)
        if r["status"] == "PASS":
            print(f"[PASS] {r['name']:<32} ({r['elapsed']:>6.2f}s)")
        elif r["meta"].get("bug"):
            print(f"[BUG ] {r['name']:<32} ({r['elapsed']:>6.2f}s)  KNOWN")
            print(f"       bug: {r['meta']['bug']}")
            for f in r["failures"]: print(f"       - {f}")
        else:
            print(f"[FAIL] {r['name']:<32} ({r['elapsed']:>6.2f}s)")
            for f in r["failures"]: print(f"       {f}")
            print(f"       log: {r['log']}")

    passed = sum(1 for r in results if r["status"] == "PASS")
    known = sum(1 for r in results if r["status"] == "FAIL" and r["meta"].get("bug"))
    regr = sum(1 for r in results if r["status"] == "FAIL" and not r["meta"].get("bug"))
    print(); print("=" * 72)
    print(f" PASS: {passed}")
    print(f" KNOWN BUGS: {known}")
    print(f" REGRESSIONS: {regr}")
    print("=" * 72)
    report = {"timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
              "pmake": str(pmake), "total": len(results),
              "passed": passed, "known_bugs": known, "regressions": regr,
              "results": [{"name": r["name"], "status": r["status"],
                           "elapsed": r["elapsed"], "failures": r["failures"],
                           "description": r["meta"].get("description", ""),
                           "bug": r["meta"].get("bug", ""), "log": r["log"]}
                          for r in results]}
    (RESULTS_DIR / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n Report: {RESULTS_DIR / 'report.json'}")
    sys.exit(0 if regr == 0 else 1)


if __name__ == "__main__":
    main()
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=DEFAULT_DIR)
    args = ap.parse_args()

    root = Path(args.dir)
    tests_dir = root / "tests"
    helpers_dir = root / "helpers"
    results_dir = root / "results"

    if not tests_dir.exists():
        print(f"[!] {tests_dir} не существует.")
        sys.exit(1)

    existing = len([d for d in tests_dir.iterdir()
                    if d.is_dir() and (d / "meta.json").exists()])
    if existing == 0:
        print(f"[!] В {tests_dir} нет тестов.")
        sys.exit(1)

    helpers_dir.mkdir(parents=True, exist_ok=True)
    results_dir.mkdir(parents=True, exist_ok=True)

    for name, content in HELPERS_EXTRA.items():
        (helpers_dir / name).write_text(content.lstrip(), encoding="utf-8")

    added, replaced = 0, 0
    for t in TESTS_EXTRA:
        td = tests_dir / t["name"]
        if td.exists():
            replaced += 1
            shutil.rmtree(td)
        else:
            added += 1
        td.mkdir(parents=True, exist_ok=True)
        for rel, content in t["files"].items():
            if not content: continue
            f = td / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_bytes(content.encode("utf-8"))
        (td / "meta.json").write_text(
            json.dumps(t["meta"], indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8")

    (root / "run.py").write_text(RUN_PY.lstrip(), encoding="utf-8")

    total = len([d for d in tests_dir.iterdir()
                 if d.is_dir() and (d / "meta.json").exists()])

    print()
    print("=" * 72)
    print(f" [OK] Edge tests fixed")
    print("=" * 72)
    print(f" Новых тестов:    {added}")
    print(f" Перезаписано:    {replaced}")
    print(f" Всего тестов:    {total}  (было {existing})")
    print()
    print(" ИСПРАВЛЕНО:")
    print("   real_order_only_and_implicit: copy.py → concat.py")
    print("   real_order_only_restat:        ожидание taskCount=1 при modify")
    print("   real_cmake_full_pattern:       ожидание taskCount=1 при touch_gen")
    print()
    print(" Запуск:")
    print(f'   {REAL_PYTHON} "{root / "run.py"}" --pmake pmake.exe')
    print("=" * 72)


if __name__ == "__main__":
    main()