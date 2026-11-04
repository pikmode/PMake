# -*- coding: utf-8 -*-
"""create_extra_tests.py — дополнительные тесты pmake (17 штук).

Добавляет новые тесты к уже сгенерированным create_real_tests.py.
Не трогает старые тесты, хелперы и файлы — только дополняет.

Также ПЕРЕЗАПИСЫВАЕТ run.py версией с поддержкой elapsedMin/elapsedMax
(нужно для real_pool_console_serial). Новый run.py полностью совместим
со старыми тестами.

Запуск:
    D:\\bin\\python3.exe create_extra_tests.py --dir C:\\Users\\abubakir\\Desktop\\TEST
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
import platform
import argparse
import json
import shutil
from pathlib import Path

DEFAULT_DIR = r"C:\Users\abubakir\Desktop\TEST"
REAL_PYTHON = "E:/python/python.exe"

IS_WINDOWS = platform.system() == "Windows"
SHELL_PREFIX = "cmd /c" if IS_WINDOWS else "sh -c"


# ============================================================
# EXTRA HELPERS (добавляются к существующим, не перезаписывая)
# ============================================================
HELPERS_EXTRA = {
    "sleep_and_touch.py": '''
import sys, time, pathlib
time.sleep(int(sys.argv[1]) / 1000.0)
p = pathlib.Path(sys.argv[2])
p.parent.mkdir(parents=True, exist_ok=True)
p.touch()
''',
    "cat_rspfile.py": '''
import sys, pathlib
src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
dst.parent.mkdir(parents=True, exist_ok=True)
if not src.exists():
    sys.exit(f"rspfile not found: {src}")
dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
''',
    "delayed_echo.py": '''
import sys, pathlib, time
time.sleep(int(sys.argv[1]) / 1000.0)
p = pathlib.Path(sys.argv[2])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(sys.argv[3] + "\\n", encoding="utf-8")
''',
}

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
# 9. GENERATOR
# ============================================================
test("real_generator_skip", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen {
    command = "${python} touch.py ${out}"
    generator = true
}
build "gen.txt" : gen
alias all : "gen.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["gen.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "delete_output",
     "before": [{"sleep": 0.3}, {"delete": ["gen.txt"]}],
     "expect": {"exit": 0, "taskCount": 1, "files": ["gen.txt"]}},
], description="generator: exists → skip; missing → rebuild")


# ============================================================
# 10. RSPFILE
# ============================================================
test("real_rspfile_basic", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen {
    command = "${python} cat_rspfile.py ${rspfile} ${out}"
    rspfile = "${out}.rsp"
    rspfile_content = "line1\\nline2\\n"
}
build "out.txt" : gen
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["out.txt"],
                                 "filesWith": {"out.txt": "line1\nline2"}}},
    {"name": "rspfile_removed", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0,
                "filesNot": ["out.txt.rsp"]}},
], description="rspfile: пишется перед командой и удаляется после")


# ============================================================
# 11. POOL CONSOLE
# ============================================================
test("real_pool_console_serial", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule console_task {
    command = "${python} sleep_and_touch.py 350 ${out}"
    pool = console
}
''' + "\n".join(f'build "c_{i}.txt" : console_task' for i in range(1, 4)) +
'\nalias all : ' + ", ".join(f'"c_{i}.txt"' for i in range(1, 4))),
}, runs=[
    {"name": "j8_serialized", "args": ["-f", "build.pmake", "-j", "8"],
     "expect": {"exit": 0, "taskCount": 3,
                "files": ["c_1.txt", "c_2.txt", "c_3.txt"],
                "elapsedMin": 0.85}},
], description="pool=console сериализует даже при -j8")


# ============================================================
# 12. RESTAT: КОНТРАСТ, ГЛУБИНА, MIXED
# ============================================================
test("real_restat_no_flag", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = false
}
build "mid.txt" : cp_preserve "src.in"
build "final.txt" : cp_preserve "mid.txt"
alias all : "final.txt"
''',
    "src.in": "constant\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
    {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
     "expect": {"exit": 0, "taskCount": 2}},
], description="restat=false: mtime не изменился, но downstream всё равно rebuild")


test("real_restat_deep_5", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
build "c0.txt" : cp_preserve "src.in"
build "c1.txt" : cp_preserve "c0.txt"
build "c2.txt" : cp_preserve "c1.txt"
build "c3.txt" : cp_preserve "c2.txt"
build "c4.txt" : cp_preserve "c3.txt"
alias all : "c4.txt"
''',
    "src.in": "deep5\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 5}},
    {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="restat chain 5: touch src → только c0 (рекурсивный skip)")


test("real_restat_mixed", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_restat {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
rule cp_plain {
    command = "${python} copy_preserve.py ${in} ${out}"
}
build "a.txt" : cp_restat "src.in"
build "b.txt" : cp_plain "a.txt"
alias all : "b.txt"
''',
    "src.in": "mixed\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
    {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="A(restat) → B(no restat): A skip → B skip по mtime")


# ============================================================
# 13. BUILD FILE CHANGE
# ============================================================
test("real_new_target", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
alias all : "a.out"
'''),
    "a.in": "new_target_a\n",
    "b.in": "new_target_b\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["a.out"]}},
    {"name": "add_b_target",
     "before": [{"sleep": 0.3},
                {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
build "b.out" : copy "b.in"
alias all : "a.out", "b.out"
''')}}],
     "expect": {"exit": 0, "taskCount": 1, "files": ["b.out"]}},
], description="добавили build в файл → старые up-to-date, только новый пересобирается")


test("real_build_file_comment", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
    {"name": "add_comment",
     "before": [{"sleep": 0.3},
                {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
// just a comment, no semantic change
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
''')}}],
     "expect": {"exit": 0, "taskCount": 0}},
], description="комментарий в build.pmake не форсит rebuild (команда не изменилась)")


# ============================================================
# 14. PATHS: SPACES, UNICODE
# ============================================================
test("real_paths_spaces", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out file.txt" : copy "in file.txt"
alias all : "out file.txt"
''',
    "in file.txt": "spaces work\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["out file.txt"],
                                 "filesWith": {"out file.txt": "spaces work"}}},
], description="пробелы в именах файлов (Windows cmd-кавычки)")


test("real_paths_unicode", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "файл.txt" : copy "исходник.txt"
alias all : "файл.txt"
''',
    "исходник.txt": "unicode ok\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["файл.txt"],
                                 "filesWith": {"файл.txt": "unicode ok"}}},
], description="Unicode в именах файлов")


# ============================================================
# 15. SANITY
# ============================================================
test("real_version", {
    "build.pmake": '''pmake_required_version = 1.0
alias all :
''',
}, runs=[
    {"name": "version", "args": ["-V"],
     "expect": {"exit": 0, "stdoutMatches": [r"\d+\.\d+"]}},
], description="pmake -V → exit 0 + номер версии")


test("real_help", {
    "build.pmake": '''pmake_required_version = 1.0
alias all :
''',
}, runs=[
    {"name": "help", "args": ["--help"],
     "expect": {"exit": 0, "stdoutMatches": ["(?i)usage|option"]}},
], description="pmake --help → exit 0 + usage")


test("real_no_build_file", {}, runs=[
    {"name": "no_file",
     "args": ["-f", "build.pmake"],
     "expect": {"exit": 1, "stderrMatches": ["(?i)cannot read|no build file|build\\.pmake"]}},
], description="нет build.pmake → exit 1 + осмысленный stderr")


# ============================================================
# 16. ORDER-ONLY И IMPLICIT INPUTS
#
# Синтаксис (как в ninja):
#   build "out" : rule "in1" "in2" | "impl1" || "order1" "order2"
#                                  ^ inputs  ^ implicit ^ order-only
#
# Семантика:
#   inputs   — участвуют в dirty (mtime + был ли собран)
#   implicit — участвуют в dirty, но НЕ в ${in} (для компилятора)
#   order-only — НЕ участвуют в dirty, только ждут завершения
# ============================================================
test("real_order_only_basic", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "stamp.txt" : copy "stamp.in"
build "out.txt" : copy "out.in" || "stamp.txt"
alias all : "out.txt"
''',
    "stamp.in": "stamp\n",
    "out.in": "out\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2,
                                 "files": ["stamp.txt", "out.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    # КЛЮЧЕВОЕ: touch order-only → stamp пересобирается, out НЕ
    {"name": "touch_order_only",
     "before": [{"sleep": 0.3}, {"touch": ["stamp.in"]}],
     "expect": {"exit": 0, "taskCount": 1,
                "files": ["stamp.txt", "out.txt"]}},
], description="order-only: touch stamp.in → stamp rebuild, out skip")


test("real_order_only_waits", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule concat { command = "${python} concat.py ${out} ${in}" }
rule gen_stamp { command = "${python} echo_to.py ${out} stamp_v1" }
build "stamp.txt" : gen_stamp
build "out.txt" : concat "in.txt" || "stamp.txt"
alias all : "out.txt"
''',
    "in.txt": "hello\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2,
                                 "files": ["stamp.txt", "out.txt"]}},
], description="order-only: out ждёт stamp (порядок задач)")


test("real_implicit_basic", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} concat.py ${out} ${in}" }
build "out.txt" : gen "main.txt" | "hdr.txt"
alias all : "out.txt"
''',
    "main.txt": "main\n",
    "hdr.txt": "hdr\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["out.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "touch_implicit",
     "before": [{"sleep": 0.3}, {"touch": ["hdr.txt"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="implicit: touch hdr → out rebuild (concat = new mtime)")

test("real_order_only_chain", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "s1.txt" : copy "s1.in"
build "s2.txt" : copy "s2.in" || "s1.txt"
build "out.txt" : copy "out.in" || "s2.txt"
alias all : "out.txt"
''',
    "s1.in": "s1\n",
    "s2.in": "s2\n",
    "out.in": "out\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                 "files": ["s1.txt", "s2.txt", "out.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    # КЛЮЧЕВОЕ: touch s1.in → s1 rebuild, s2 и out НЕ (рекурсивно не propagates)
    {"name": "touch_s1",
     "before": [{"sleep": 0.3}, {"touch": ["s1.in"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="order-only chain: touch s1 → только s1 rebuild")


# ============================================================
# RUNNER — перезаписывает run.py, добавляет elapsedMin/elapsedMax
# ============================================================
RUN_PY = r'''#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# run.py — совместим со старыми и новыми тестами (elapsedMin/Max).
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


# ============================================================
# MAIN
# ============================================================
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
        print("    Сначала запусти: python create_real_tests.py --dir " + str(root))
        sys.exit(1)

    existing = len([d for d in tests_dir.iterdir()
                    if d.is_dir() and (d / "meta.json").exists()])
    if existing == 0:
        print(f"[!] В {tests_dir} нет тестов.")
        print("    Сначала запусти: python create_real_tests.py --dir " + str(root))
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
    print(f" [OK] Extra tests added")
    print("=" * 72)
    print(f" Новых тестов:    {added}")
    print(f" Перезаписано:    {replaced}")
    print(f" Всего тестов:    {total}  (было {existing})")
    print()
    print(" Хелперы добавлены: " + ", ".join(HELPERS_EXTRA.keys()))
    print(" run.py перезаписан (добавлены elapsedMin/elapsedMax).")
    print()
    print(" Новые тесты:")
    for t in TESTS_EXTRA:
        print(f"   - {t['name']:<28} {t['meta']['description'][:50]}")
    print()
    print(" Запуск:")
    print(f'   {REAL_PYTHON} "{root / "run.py"}" --pmake pmake.exe')
    print()
    print(" Только новые:")
    print(f'   {REAL_PYTHON} "{root / "run.py"}" --pmake pmake.exe --filter "real_order_only*"')
    print(f'   {REAL_PYTHON} "{root / "run.py"}" --pmake pmake.exe --filter "real_implicit*"')
    print("=" * 72)


if __name__ == "__main__":
    main()