#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""setup_all_tests.py — генерирует модульную структуру тестов PMake.

Создаёт в CWD:
  run.py
  build.pmake
  src/
    __init__.py
    _common.py
    helpers.py
    tests_basic.py          (3)
    tests_incremental.py    (9)
    tests_restat.py         (9)
    tests_order_only.py     (9)
    tests_phony.py          (4)
    tests_depfile.py        (4)
    tests_command_hash.py   (3)
    tests_include.py        (3)
    tests_errors.py         (4)
    tests_parallel.py       (5)
    tests_paths.py          (2)
    tests_modules.py        (4)
    tests_builtins.py       (2)
    tests_pool.py           (2)
    tests_rspfile.py        (1)
    tests_generator.py      (2)
    tests_cmake.py          (1)
    tests_misc.py           (14)
    tests_hard.py           (5)
    generate.py
    runner.py

Итого: 86 тестов.

Запуск:
    python setup_all_tests.py
    python run.py --pmake pmake.exe
"""
from __future__ import annotations
import sys as _sys
try:
    _sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    _sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import sys
from pathlib import Path

FILES = {}

# ============================================================
# run.py
# ============================================================
FILES["run.py"] = '''#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run.py — entry point.

Без флагов: генерирует тестовый проект в CWD + прогоняет тесты.
--generate-only: только генерирует (для build.pmake).
--pmake PATH, --filter PATTERN, --keep-temp.

ВСЕ пути относительно CWD (место вызова). Модули src/ — относительно __file__.
"""
from __future__ import annotations
import sys as _sys
try:
    _sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    _sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from src.generate import generate_project
from src import runner


def main() -> int:
    generate_only = "--generate-only" in sys.argv
    root = Path.cwd()
    generate_project(root)
    if generate_only:
        print(f"[OK] test project generated in {root}")
        return 0
    return runner.main(root)


if __name__ == "__main__":
    sys.exit(main())
'''

# ============================================================
# build.pmake
# ============================================================
_SRC_FILES = " ".join(f'"src/{n}"' for n in [
    "__init__.py", "_common.py", "helpers.py",
    "tests_basic.py", "tests_incremental.py", "tests_restat.py",
    "tests_order_only.py", "tests_phony.py", "tests_depfile.py",
    "tests_command_hash.py", "tests_include.py", "tests_errors.py",
    "tests_parallel.py", "tests_paths.py", "tests_modules.py",
    "tests_builtins.py", "tests_pool.py", "tests_rspfile.py",
    "tests_generator.py", "tests_cmake.py", "tests_misc.py",
    "tests_hard.py", "generate.py", "runner.py",
])
FILES["build.pmake"] = f'''pmake_required_version = 1.0

python = "E:/python/python.exe"

rule gen_tests {{
    command     = "${{python}} run.py --generate-only"
    description = "generating test project"
    restat      = true
}}

build ".generated" : gen_tests "run.py" {_SRC_FILES}

alias all : ".generated"
'''

# ============================================================
# src/__init__.py
# ============================================================
FILES["src/__init__.py"] = ""

# ============================================================
# src/_common.py
# ============================================================
FILES["src/_common.py"] = '''# -*- coding: utf-8 -*-
"""_common.py — общие утилиты."""
from __future__ import annotations
import platform

REAL_PYTHON = "E:/python/python.exe"
IS_WINDOWS = platform.system() == "Windows"
SHELL_PREFIX = "cmd /c" if IS_WINDOWS else "sh -c"


def pp(s: str) -> str:
    return s.replace("@PYTHON@", REAL_PYTHON)


def test(name, files, runs, description="", bug="", timeout=60, args=None):
    return {
        "name": name,
        "files": {k: pp(v) if isinstance(v, str) else v for k, v in files.items()},
        "meta": {
            "description": description,
            "bug": bug,
            "timeoutSec": timeout,
            "args": args or ["-f", "build.pmake"],
            "runs": runs,
        },
    }
'''

# ============================================================
# src/helpers.py
# ============================================================
FILES["src/helpers.py"] = '''# -*- coding: utf-8 -*-
"""helpers.py — Python-хелперы для build.pmake правил."""

HELPERS = {
    "touch.py": """
import sys, pathlib
for arg in sys.argv[1:]:
    p = pathlib.Path(arg)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.touch()
""",
    "echo_to.py": """
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(sys.argv[2] + "\\\\n", encoding="utf-8")
""",
    "copy.py": """
import sys, shutil, pathlib
pathlib.Path(sys.argv[2]).parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(sys.argv[1], sys.argv[2])
""",
    "copy_preserve.py": """
import sys, pathlib, os
src = pathlib.Path(sys.argv[1]); dst = pathlib.Path(sys.argv[2])
dst.parent.mkdir(parents=True, exist_ok=True)
content = src.read_bytes()
if dst.exists():
    st = dst.stat(); old = dst.read_bytes()
    if old == content:
        dst.write_bytes(content)
        os.utime(dst, ns=(st.st_atime_ns, st.st_mtime_ns)); sys.exit(0)
    dst.write_bytes(content)
else:
    dst.write_bytes(content)
""",
    "copy_preserve_multi.py": """
import sys, pathlib, os
src = pathlib.Path(sys.argv[1]); content = src.read_bytes()
for out_s in sys.argv[2:]:
    out = pathlib.Path(out_s)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        st = out.stat(); old = out.read_bytes()
        if old == content:
            out.write_bytes(content)
            os.utime(out, ns=(st.st_atime_ns, st.st_mtime_ns)); continue
    out.write_bytes(content)
""",
    "cc_make_dep.py": """
import sys, pathlib
src, out = sys.argv[1], sys.argv[2]
hdr = sys.argv[3] if len(sys.argv) > 3 else ""
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
if hdr:
    pathlib.Path(out + ".d").write_text(f"{out}: {src} {hdr}\\\\n", encoding="utf-8")
""",
    "cc_maybe_fail.py": """
import sys, pathlib, time
src = pathlib.Path(sys.argv[1]); out = sys.argv[2]
if src.exists() and "FAIL" in src.read_text(encoding="utf-8"):
    time.sleep(0.2)
    print(f"intentional failure for {out}", file=sys.stderr); sys.exit(1)
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
pathlib.Path(out + ".d").write_text(f"{out}: {src.name} common.h\\\\n", encoding="utf-8")
""",
    "concat.py": """
import sys, pathlib
out = pathlib.Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("".join(pathlib.Path(p).read_text(encoding="utf-8") for p in sys.argv[2:]), encoding="utf-8")
""",
    "multi_out.py": """
import sys, pathlib
for out in sys.argv[1:]:
    p = pathlib.Path(out)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(f"from_multi_out_{p.name}\\\\n", encoding="utf-8")
""",
    "fail.py": """
import sys
print("intentional failure", file=sys.stderr)
sys.exit(1)
""",
    "sleep.py": """
import sys, time
time.sleep(int(sys.argv[1]) / 1000.0)
""",
    "sleep_and_touch.py": """
import sys, time, pathlib
time.sleep(int(sys.argv[1]) / 1000.0)
p = pathlib.Path(sys.argv[2])
p.parent.mkdir(parents=True, exist_ok=True)
p.touch()
""",
    "check_stamp.py": """
import sys, pathlib
stamp = pathlib.Path(sys.argv[1])
if not stamp.exists():
    print(f"ORDER VIOLATION: {stamp} not created yet", file=sys.stderr); sys.exit(1)
src = pathlib.Path(sys.argv[2]); dst = pathlib.Path(sys.argv[3])
dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
""",
    "check_two.py": """
import sys, pathlib
s1 = pathlib.Path(sys.argv[1]); s2 = pathlib.Path(sys.argv[2])
if not s1.exists():
    print(f"ORDER VIOLATION: {s1} not created yet", file=sys.stderr); sys.exit(1)
if not s2.exists():
    print(f"ORDER VIOLATION: {s2} not created yet", file=sys.stderr); sys.exit(1)
dst = pathlib.Path(sys.argv[3])
dst.write_text(s1.read_text(encoding="utf-8") + s2.read_text(encoding="utf-8"), encoding="utf-8")
""",
    "write_all.py": """
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(" ".join(sys.argv[2:]) + "\\\\n", encoding="utf-8")
""",
    "make_dep.py": """
import sys, pathlib
out = sys.argv[1]
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
header = sys.argv[2] if len(sys.argv) > 2 else ""
if header:
    pathlib.Path(out + ".d").write_text(f"{out}: {header}\\\\n", encoding="utf-8")
""",
    "restat.py": """
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.touch()
p.write_text("constant\\\\n", encoding="utf-8")
""",
    "cat_rspfile.py": """
import sys, pathlib
src = pathlib.Path(sys.argv[1]); dst = pathlib.Path(sys.argv[2])
dst.parent.mkdir(parents=True, exist_ok=True)
if not src.exists():
    sys.exit(f"rspfile not found: {src}")
dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
""",
    "delayed_echo.py": """
import sys, pathlib, time
time.sleep(int(sys.argv[1]) / 1000.0)
p = pathlib.Path(sys.argv[2])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(sys.argv[3] + "\\\\n", encoding="utf-8")
""",
}
'''

# ============================================================
# src/generate.py
# ============================================================
FILES["src/generate.py"] = '''# -*- coding: utf-8 -*-
"""generate.py — собирает все TESTS и пишет тестовый проект в CWD."""
from __future__ import annotations
import json
import shutil
import time
from pathlib import Path

from .helpers import HELPERS
from .tests_basic import TESTS as T_BASIC
from .tests_incremental import TESTS as T_INCR
from .tests_restat import TESTS as T_RESTAT
from .tests_order_only import TESTS as T_ORDER
from .tests_phony import TESTS as T_PHONY
from .tests_depfile import TESTS as T_DEPFILE
from .tests_command_hash import TESTS as T_HASH
from .tests_include import TESTS as T_INCLUDE
from .tests_errors import TESTS as T_ERRORS
from .tests_parallel import TESTS as T_PARALLEL
from .tests_paths import TESTS as T_PATHS
from .tests_modules import TESTS as T_MODULES
from .tests_builtins import TESTS as T_BUILTINS
from .tests_pool import TESTS as T_POOL
from .tests_rspfile import TESTS as T_RSPFILE
from .tests_generator import TESTS as T_GEN
from .tests_cmake import TESTS as T_CMAKE
from .tests_misc import TESTS as T_MISC
from .tests_hard import TESTS as T_HARD

ALL_TESTS = (
    T_BASIC + T_INCR + T_RESTAT + T_ORDER + T_PHONY + T_DEPFILE +
    T_HASH + T_INCLUDE + T_ERRORS + T_PARALLEL + T_PATHS +
    T_MODULES + T_BUILTINS + T_POOL + T_RSPFILE + T_GEN +
    T_CMAKE + T_MISC + T_HARD
)


def generate_project(root: Path) -> None:
    tests_dir = root / "tests"
    helpers_dir = root / "helpers"
    results_dir = root / "results"

    if tests_dir.exists():
        shutil.rmtree(tests_dir)
    tests_dir.mkdir(parents=True)
    helpers_dir.mkdir(parents=True, exist_ok=True)
    results_dir.mkdir(parents=True, exist_ok=True)

    for name, content in HELPERS.items():
        (helpers_dir / name).write_text(content.lstrip(), encoding="utf-8")

    for t in ALL_TESTS:
        td = tests_dir / t["name"]
        td.mkdir(parents=True, exist_ok=True)
        for rel, content in t["files"].items():
            f = td / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text(content, encoding="utf-8")
        (td / "meta.json").write_text(
            json.dumps(t["meta"], indent=2, ensure_ascii=False) + "\\n",
            encoding="utf-8",
        )

    (root / ".generated").write_text(
        f"generated at {time.strftime('%Y-%m-%dT%H:%M:%S')}\\n"
        f"tests: {len(ALL_TESTS)}\\n"
        f"helpers: {len(HELPERS)}\\n",
        encoding="utf-8",
    )
'''

# ============================================================
# src/runner.py
# ============================================================
FILES["src/runner.py"] = '''# -*- coding: utf-8 -*-
"""runner.py — прогоняет tests/, пишет results/."""
from __future__ import annotations
import argparse
import fnmatch
import json
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


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


def run_pmake(exe, args, cwd, timeout, kill_after_ms=None):
    t0 = time.time()
    if kill_after_ms is not None:
        try:
            p = subprocess.Popen(
                [str(exe)] + list(args), cwd=str(cwd),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            time.sleep(kill_after_ms / 1000.0)
            if p.poll() is None: p.kill()
            try:
                out_b, err_b = p.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                p.kill(); out_b, err_b = p.communicate()
            return {"exit": p.returncode,
                    "stdout": (out_b or b"").decode("utf-8", "replace"),
                    "stderr": (err_b or b"").decode("utf-8", "replace"),
                    "elapsed": time.time() - t0, "timedOut": False, "killed": True}
        except Exception as e:
            return {"exit": -998, "stdout": "",
                    "stderr": f"[runner/kill] {type(e).__name__}: {e}",
                    "elapsed": time.time() - t0, "timedOut": False}
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
                "stderr": dec(e.stderr) + f"\\n[TIMEOUT {timeout}s]",
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


def run_test(pmake, test_dir, helpers_dir, results_dir, keep_temp):
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
        kill_after_ms = run.get("kill_after_ms")
        result = run_pmake(pmake, args, work, timeout, kill_after_ms=kill_after_ms)
        total_elapsed += result["elapsed"]
        logs.append(f"=== {run_name} ===\\nargs: {args}\\nexit: {result['exit']}\\n"
                    f"killed: {result.get('killed', False)}\\n"
                    f"elapsed: {result['elapsed']:.2f}s\\n\\n"
                    f"--- STDOUT ---\\n{result['stdout']}\\n"
                    f"--- STDERR ---\\n{result['stderr']}\\n")
        fails = check_expect(run.get("expect", {}), result, work)
        if fails:
            all_fails.extend([f"[{run_name}] {x}" for x in fails])
            if not run.get("continue", False): break

    results_dir.mkdir(parents=True, exist_ok=True)
    log_path = results_dir / f"{name}.log"
    header = f"=== TEST: {name} ===\\n"
    if meta.get("description"): header += f"Description: {meta['description']}\\n"
    if meta.get("bug"): header += f"Known bug: {meta['bug']}\\n"
    log_path.write_text(header + "\\n" + "\\n".join(logs), encoding="utf-8")
    if not keep_temp: shutil.rmtree(temp_root, ignore_errors=True)
    return {"name": name, "status": "PASS" if not all_fails else "FAIL",
            "failures": all_fails, "elapsed": total_elapsed,
            "meta": meta, "log": str(log_path)}


def main(root):
    ap = argparse.ArgumentParser()
    ap.add_argument("--pmake")
    ap.add_argument("--filter", default="*")
    ap.add_argument("--keep-temp", action="store_true")
    args, _ = ap.parse_known_args()

    pmake = find_pmake(args.pmake)
    tests_dir = root / "tests"
    helpers_dir = root / "helpers"
    results_dir = root / "results"
    results_dir.mkdir(parents=True, exist_ok=True)
    for f in results_dir.glob("*.log"): f.unlink()
    for f in results_dir.glob("*.json"): f.unlink()

    test_dirs = sorted(d for d in tests_dir.iterdir()
                       if d.is_dir() and (d / "meta.json").exists())
    if args.filter != "*":
        test_dirs = [d for d in test_dirs if fnmatch.fnmatch(d.name, args.filter)]

    print("=" * 72)
    print(f" PMAKE TEST SUITE ({len(test_dirs)} tests)")
    print(f" pmake: {pmake}")
    print("=" * 72); print()

    results = []
    for td in test_dirs:
        r = run_test(pmake, td, helpers_dir, results_dir, args.keep_temp)
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
    (results_dir / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\\n Report: {results_dir / 'report.json'}")
    return 0 if regr == 0 and known == 0 else 1
'''

# ============================================================
# tests_*.py  (все 19 модулей)
# ============================================================

TESTS_BASIC = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("basic", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo {
    command = "${python} echo_to.py ${out} hello"
    description = "ECHO ${out}"
}
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "files": ["out.txt"], "filesWith": {"out.txt": "hello"}}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="Минимум: одно правило, один build, alias"),

    test("two_builds", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "a.txt" : echo
build "b.txt" : echo
alias all : "a.txt", "b.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["a.txt", "b.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="Два независимых build"),

    test("alias_multi", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} y" }
build "x.txt" : echo
build "y.txt" : echo
build "z.txt" : echo
alias all : "x.txt", "y.txt", "z.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                     "files": ["x.txt", "y.txt", "z.txt"]}},
    ], description="alias с 3 targets"),
]
'''

TESTS_INCR = '''# -*- coding: utf-8 -*-
from ._common import test, pp

TESTS = [
    test("incr_source", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out.c" : copy "src.c"
alias all : "out.c"
\'\'\',
        "src.c": "int v = 1;\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.c"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "modify_src", "before": [{"sleep": 0.5}, {"modify": {"src.c": "int v = 2;\\n"}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.c": "int v = 2;"}}},
    ], description="Изменение src.c → out.c пересобирается"),

    test("incr_delete_output", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} z" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
        {"name": "delete_and_rebuild",
         "before": [{"sleep": 0.5}, {"delete": ["out.txt"]}],
         "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
    ], description="Удаление output → пересборка"),

    test("incr_chain", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
build "b.out" : copy "a.out"
build "c.out" : copy "b.out"
alias all : "c.out"
\'\'\',
        "a.in": "deep\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                     "files": ["a.out", "b.out", "c.out"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_a_in", "before": [{"sleep": 0.5}, {"touch": ["a.in"]}],
         "expect": {"exit": 0, "taskCount": 3}},
    ], description="a.in → a.out → b.out → c.out"),

    test("real_incr_chain_5", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
build "b.out" : copy "a.out"
build "c.out" : copy "b.out"
build "d.out" : copy "c.out"
build "e.out" : copy "d.out"
alias all : "e.out"
\'\'\',
        "a.in": "deep5\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 5}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_a", "before": [{"sleep": 0.3}, {"touch": ["a.in"]}],
         "expect": {"exit": 0, "taskCount": 5}},
    ], description="5 уровней"),

    test("real_incr_diamond", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "b.out" : copy "a.in"
build "c.out" : copy "a.in"
build "d.out" : copy "b.out"
build "e.out" : copy "c.out"
build "f.out" : concat "d.out" "e.out"
alias all : "f.out"
\'\'\',
        "a.in": "diamond\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 5}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_root", "before": [{"sleep": 0.3}, {"touch": ["a.in"]}],
         "expect": {"exit": 0, "taskCount": 5}},
    ], description="Diamond граф"),

    test("real_incr_two_branches", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "x.o" : copy "x.c"
build "y.o" : copy "y.c"
build "app" : concat "x.o" "y.o"
alias all : "app"
\'\'\',
        "x.c": "x\\n", "y.c": "y\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3}},
        {"name": "touch_x", "before": [{"sleep": 0.3}, {"touch": ["x.c"]}],
         "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_y", "before": [{"sleep": 0.3}, {"touch": ["y.c"]}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="Две ветки"),

    test("real_incr_cpp_header", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cc {
    command = "${python} cc_make_dep.py ${in} ${out} common.h"
    depfile = "${out}.d"
    deps = "gcc"
}
rule link { command = "${python} concat.py ${out} ${in}" }
build "main.o" : cc "main.c"
build "util.o" : cc "util.c"
build "app" : link "main.o" "util.o"
alias all : "app"
\'\'\',
        "main.c": "main\\n", "util.c": "util\\n", "common.h": "// v1\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3}},
        {"name": "touch_h", "before": [{"sleep": 0.3}, {"touch": ["common.h"]}],
         "expect": {"exit": 0, "taskCount": 3}},
        {"name": "touch_main", "before": [{"sleep": 0.3}, {"touch": ["main.c"]}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="C++: .h → оба .o + app"),

    test("real_incr_partial_delete", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
\'\'\' + "\\n".join(f\'build "o_{i}.out" : copy "o_{i}.in"\' for i in range(1, 11)) +
\'\\nalias all : \' + ", ".join(f\'"o_{i}.out"\' for i in range(1, 11))),
        **{f"o_{i}.in": f"data_{i}\\n" for i in range(1, 11)},
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 10}},
        {"name": "delete_3",
         "before": [{"sleep": 0.3}, {"delete": ["o_3.out", "o_7.out", "o_9.out"]}],
         "expect": {"exit": 0, "taskCount": 3}},
    ], description="10 → удалили 3 → только 3 пересобираются"),

    test("real_incr_deep_10", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
\'\'\' + "\\n".join(f\'build "c{i}.out" : copy \' +
                (\'"src.in"\' if i == 0 else f\'"c{i-1}.out"\' ) for i in range(0, 10)) +
\'\\nalias all : "c9.out"\'),
        "src.in": "deep10\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 10}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 10}},
    ], description="10-уровневая цепочка"),
]
'''

TESTS_RESTAT = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("restat", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule touch_always { command = "${python} restat.py ${out}" restat = true }
rule copy { command = "${python} copy.py ${in} ${out}" }
build "mid.txt" : touch_always
build "final.txt" : copy "mid.txt"
alias all : "final.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2,
                                     "files": ["mid.txt", "final.txt"]}},
        {"name": "noop_restat", "before": [{"sleep": 0.5}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="restat=true: touch → content unchanged → final skip"),

    test("real_restat_skip", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve { command = "${python} copy_preserve.py ${in} ${out}" restat = true }
build "mid.txt" : cp_preserve "src.in"
build "final.txt" : cp_preserve "mid.txt"
alias all : "final.txt"
\'\'\',
        "src.in": "constant\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="restat: touch src → final пропускается"),

    test("real_restat_chain_3", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve { command = "${python} copy_preserve.py ${in} ${out}" restat = true }
build "m1.txt" : cp_preserve "src.in"
build "m2.txt" : cp_preserve "m1.txt"
build "m3.txt" : cp_preserve "m2.txt"
alias all : "m3.txt"
\'\'\',
        "src.in": "chain3\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="restat chain 3"),

    test("real_restat_content_change", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve { command = "${python} copy_preserve.py ${in} ${out}" restat = true }
build "mid.txt" : cp_preserve "src.in"
build "final.txt" : cp_preserve "mid.txt"
alias all : "final.txt"
\'\'\',
        "src.in": "v1\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "modify_src", "before": [{"sleep": 0.3}, {"modify": {"src.in": "v2\\n"}}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="restat: контент изменился → оба пересобираются"),

    test("real_restat_multi_output", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_mp { command = "${python} copy_preserve_multi.py ${in} ${out}" restat = true }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "a.txt", "b.txt" : gen_mp "src.in"
build "final.txt" : concat "a.txt" "b.txt"
alias all : "final.txt"
\'\'\',
        "src.in": "multi_restat\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="restat + multi-output"),

    test("real_restat_with_depfile", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cc {
    command = "${python} cc_make_dep.py ${in} ${out} common.h"
    depfile = "${out}.d"
    deps = "gcc"
    restat = true
}
rule link { command = "${python} concat.py ${out} ${in}" }
build "main.o" : cc "main.c"
build "app" : link "main.o"
alias all : "app"
\'\'\',
        "main.c": "main\\n", "common.h": "// v1\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_h", "before": [{"sleep": 0.3}, {"touch": ["common.h"]}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="restat + depfile"),

    test("real_restat_no_flag", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve { command = "${python} copy_preserve.py ${in} ${out}" restat = false }
build "mid.txt" : cp_preserve "src.in"
build "final.txt" : cp_preserve "mid.txt"
alias all : "final.txt"
\'\'\',
        "src.in": "constant\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="restat=false: downstream всё равно rebuild"),

    test("real_restat_deep_5", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve { command = "${python} copy_preserve.py ${in} ${out}" restat = true }
build "c0.txt" : cp_preserve "src.in"
build "c1.txt" : cp_preserve "c0.txt"
build "c2.txt" : cp_preserve "c1.txt"
build "c3.txt" : cp_preserve "c2.txt"
build "c4.txt" : cp_preserve "c3.txt"
alias all : "c4.txt"
\'\'\',
        "src.in": "deep5\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 5}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="restat chain 5"),

    test("real_restat_mixed", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_restat { command = "${python} copy_preserve.py ${in} ${out}" restat = true }
rule cp_plain { command = "${python} copy_preserve.py ${in} ${out}" }
build "a.txt" : cp_restat "src.in"
build "b.txt" : cp_plain "a.txt"
alias all : "b.txt"
\'\'\',
        "src.in": "mixed\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="A(restat) → B(no restat)"),
]
'''

TESTS_ORDER = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_implicit_basic", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} concat.py ${out} ${in}" }
build "out.txt" : gen "main.txt" | "hdr.txt"
alias all : "out.txt"
\'\'\',
        "main.txt": "main\\n", "hdr.txt": "hdr\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_implicit", "before": [{"sleep": 0.3}, {"touch": ["hdr.txt"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="implicit: touch hdr → out rebuild"),

    test("real_order_only_basic", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "stamp.txt" : copy "stamp.in"
build "out.txt" : copy "out.in" || "stamp.txt"
alias all : "out.txt"
\'\'\',
        "stamp.in": "stamp\\n", "out.in": "out\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_order_only", "before": [{"sleep": 0.3}, {"touch": ["stamp.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="order-only: touch stamp → stamp rebuild, out skip"),

    test("real_order_only_waits", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule concat { command = "${python} concat.py ${out} ${in}" }
rule gen_stamp { command = "${python} echo_to.py ${out} stamp_v1" }
build "stamp.txt" : gen_stamp
build "out.txt" : concat "in.txt" || "stamp.txt"
alias all : "out.txt"
\'\'\',
        "in.txt": "hello\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
    ], description="order-only: out ждёт stamp"),

    test("real_order_only_chain", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "s1.txt" : copy "s1.in"
build "s2.txt" : copy "s2.in" || "s1.txt"
build "out.txt" : copy "out.in" || "s2.txt"
alias all : "out.txt"
\'\'\',
        "s1.in": "s1\\n", "s2.in": "s2\\n", "out.in": "out\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3, "files": ["s1.txt", "s2.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_s1", "before": [{"sleep": 0.3}, {"touch": ["s1.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="order-only chain"),

    test("real_order_only_restat", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_restat { command = "${python} copy_preserve.py ${in} ${out}" restat = true }
rule check_stamp { command = "${python} check_stamp.py stamp.txt ${in} ${out}" }
build "stamp.txt" : gen_restat "stamp.in"
build "out.txt" : check_stamp "out.in" || "stamp.txt"
alias all : "out.txt"
\'\'\',
        "stamp.in": "constant\\n", "out.in": "out\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_stamp_skip", "before": [{"sleep": 0.3}, {"touch": ["stamp.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "modify_stamp_order_only_no_propagate",
         "before": [{"sleep": 0.3}, {"modify": {"stamp.in": "changed\\n"}}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="order-only + restat"),

    test("real_order_only_generator", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_once { command = "${python} touch.py ${out}" generator = true }
rule check_stamp { command = "${python} check_stamp.py stamp.txt ${in} ${out}" }
build "stamp.txt" : gen_once
build "out.txt" : check_stamp "out.in" || "stamp.txt"
alias all : "out.txt"
\'\'\',
        "out.in": "out\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_stamp", "before": [{"sleep": 0.3}, {"delete": ["stamp.txt"]}],
         "expect": {"exit": 0, "taskCount": 1, "files": ["stamp.txt", "out.txt"]}},
    ], description="order-only + generator"),

    test("real_order_only_missing", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out.txt" : copy "in.txt" || "nonexistent.stamp"
alias all : "out.txt"
\'\'\',
        "in.txt": "in\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"],
                                     "filesWith": {"out.txt": "in"}}},
    ], description="missing order-only: leaf игнорируется"),

    test("real_order_only_multi", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule slow_touch { command = "${python} sleep_and_touch.py 300 ${out}" }
rule check_two { command = "${python} check_two.py s1.txt s2.txt ${out}" }
build "s1.txt" : slow_touch
build "s2.txt" : slow_touch
build "out.txt" : check_two || "s1.txt" "s2.txt"
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "j2_ordered", "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "taskCount": 3, "files": ["s1.txt", "s2.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="multi order-only"),

    test("real_order_only_and_implicit", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_out { command = "${python} concat.py ${out} ${in}" }
rule gen_stamp { command = "${python} touch.py ${out}" }
build "stamp.txt" : gen_stamp
build "out.txt" : gen_out "main.txt" | "hdr.txt" || "stamp.txt"
alias all : "out.txt"
\'\'\',
        "main.txt": "main\\n", "hdr.txt": "hdr\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_implicit", "before": [{"sleep": 0.3}, {"touch": ["hdr.txt"]}],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "delete_stamp", "before": [{"sleep": 0.3}, {"delete": ["stamp.txt"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="implicit + order-only"),
]
'''

TESTS_PHONY = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_phony_order_basic", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule slow_touch { command = "${python} sleep_and_touch.py 400 ${out}" }
rule check_stamp { command = "${python} check_stamp.py stamp.txt ${in} ${out}" }
build "stamp.txt" : slow_touch
build "phony_stamp" : phony "stamp.txt"
build "out.txt" : check_stamp "out.in" || "phony_stamp"
alias all : "out.txt"
\'\'\',
        "out.in": "out\\n",
    }, runs=[
        {"name": "j2_ordered", "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="phony (input): порядок через BFS"),

    test("real_phony_order_transitive", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule slow_touch { command = "${python} sleep_and_touch.py 300 ${out}" }
rule check_two { command = "${python} check_two.py step1.txt step2.txt ${out}" }
build "step1.txt" : slow_touch
build "step2.txt" : slow_touch
build "phony1" : phony "step1.txt"
build "phony2" : phony "step2.txt" "phony1"
build "out.txt" : check_two || "phony2"
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "j2_ordered", "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "taskCount": 3, "files": ["step1.txt", "step2.txt", "out.txt"]}},
    ], description="phony chain (input)"),

    test("real_phony_order_cmake", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule slow_touch { command = "${python} sleep_and_touch.py 400 ${out}" }
rule check_stamp { command = "${python} check_stamp.py autogen_stamp ${in} ${out}" }
build "autogen_stamp" : slow_touch
build "cmake_obj_deps" : phony || "autogen_stamp"
build "out.txt" : check_stamp "out.in" || "cmake_obj_deps"
alias all : "out.txt"
\'\'\',
        "out.in": "out\\n",
    }, runs=[
        {"name": "j2_ordered", "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "taskCount": 2, "files": ["autogen_stamp", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="CMake-паттерн phony ||"),

    test("real_phony_order_noop", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "stamp.txt" : copy "stamp.in"
build "phony_stamp" : phony "stamp.txt"
build "out.txt" : copy "out.in" || "phony_stamp"
alias all : "out.txt"
\'\'\',
        "stamp.in": "stamp\\n", "out.in": "out\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_stamp", "before": [{"sleep": 0.3}, {"touch": ["stamp.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="phony + order-only: noop не ломается"),
]
'''

TESTS_DEPFILE = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("depfile_basic", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule mk {
    command = "${python} make_dep.py ${out} hdr.h"
    depfile = "${out}.d"
    deps = "gcc"
}
build "out.o" : mk "hdr.h"
alias all : "out.o"
\'\'\',
        "hdr.h": "/* v1 */\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.o"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_header", "before": [{"sleep": 0.5}, {"touch": ["hdr.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile: touch заголовка → пересборка"),

    test("depfile_unrelated", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule mk {
    command = "${python} make_dep.py ${out} hdr.h"
    depfile = "${out}.d"
    deps = "gcc"
}
build "out.o" : mk "hdr.h"
alias all : "out.o"
\'\'\',
        "hdr.h": "/* v1 */\\n", "other.h": "/* unrelated */\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "touch_unrelated",
         "before": [{"sleep": 0.5}, {"touch": ["other.h"]}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="depfile: touch нерелевантного → НЕ пересборка"),

    test("real_depfile_lost", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_make_dep.py ${in} ${out} common.h" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
\'\'\',
        "main.c": "main\\n", "common.h": "// v1\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "lost_d_touch_h",
         "before": [{"sleep": 0.3}, {"delete": ["Project-cache.pmake", "main.o.d"]},
                    {"touch": ["common.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile lost → force rebuild"),

    test("real_depfile_persists", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_make_dep.py ${in} ${out} hdr.h" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
\'\'\',
        "main.c": "main\\n", "hdr.h": "// v1\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "touch_hdr", "before": [{"sleep": 0.3}, {"touch": ["hdr.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile persists"),
]
'''

TESTS_HASH = '''# -*- coding: utf-8 -*-
from ._common import test, pp

TESTS = [
    test("real_command_change", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} -O2" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "-O2"}}},
        {"name": "change_command",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} -O3" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\')}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "-O3"}}},
    ], description="command hash: -O2 → -O3 → rebuild"),

    test("real_command_var_change", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
flags = "-O2"
rule echo { command = "${python} echo_to.py ${out} ${flags}" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "change_var",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
flags = "-O3"
rule echo { command = "${python} echo_to.py ${out} ${flags}" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\')}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "-O3"}}},
    ], description="command hash: flags -O2 → -O3"),

    test("real_command_hash_chain_3", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule with_flag { command = "${python} echo_to.py ${out} FLAG_V1" }
build "a.out" : copy "src.in"
build "b.out" : copy "a.out"
build "c.out" : with_flag "b.out"
alias all : "c.out"
\'\'\'),
        "src.in": "chain\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3, "filesWith": {"c.out": "FLAG_V1"}}},
        {"name": "change_c_only",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule with_flag { command = "${python} echo_to.py ${out} FLAG_V2" }
build "a.out" : copy "src.in"
build "b.out" : copy "a.out"
build "c.out" : with_flag "b.out"
alias all : "c.out"
\'\'\')}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"c.out": "FLAG_V2"}}},
    ], description="command hash chain"),
]
'''

TESTS_INCLUDE = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("include_basic", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
include "sub.pmake"
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
        "sub.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} from_sub" }
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "from_sub"}}},
    ], description="include простого файла"),

    test("include_relative", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
include "subdir/part.pmake"
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
        "subdir/part.pmake": \'pmake_required_version = 1.0\\ninclude "nested.pmake"\\n\',
        "subdir/nested.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} nested_ok" }
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "nested_ok"}}},
    ], description="include с относительным путём"),

    test("include_cycle", {
        "build.pmake": \'pmake_required_version = 1.0\\ninclude "a.pmake"\\n\',
        "a.pmake": \'pmake_required_version = 1.0\\ninclude "b.pmake"\\n\',
        "b.pmake": \'pmake_required_version = 1.0\\ninclude "a.pmake"\\n\',
    }, runs=[
        {"name": "cycle_detected", "expect": {"exit": 1, "stderrHas": ["ycl"]}},
    ], description="include cycle → ошибка, не крашить"),
]
'''

TESTS_ERRORS = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("error_cycle", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a" : copy "b"
build "b" : copy "a"
alias all : "a"
\'\'\',
    }, runs=[
        {"name": "cycle_should_fail", "expect": {"exit": 1, "stderrHas": ["ycl"]}},
    ], description="цикл в графе → ошибка"),

    test("error_missing_rule", {
        "build.pmake": \'pmake_required_version = 1.0\\nbuild "out.txt" : nonexistent_rule\\nalias all : "out.txt"\\n\',
    }, runs=[
        {"name": "missing_rule_should_fail", "expect": {"exit": 1}},
    ], description="несуществующее правило → ошибка"),

    test("error_bad_syntax", {
        "build.pmake": \'pmake_required_version = 1.0\\nbuild "out.txt" :\\nalias all : "out.txt"\\n\',
    }, runs=[
        {"name": "bad_syntax_should_fail", "expect": {"exit": 1}},
    ], description="синтаксическая ошибка → exit 1"),

    test("real_error_recovery", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_maybe_fail.py ${in} ${out}" }
rule link { command = "${python} concat.py ${out} ${in}" }
build "a.o" : cc "a.c"
build "b.o" : cc "b.c"
build "app" : link "a.o" "b.o"
alias all : "app"
\'\'\',
        "a.c": "int a;\\n", "b.c": "FAIL\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 1, "files": ["a.o"], "filesNot": ["b.o", "app"]}},
        {"name": "fix_and_retry",
         "before": [{"sleep": 0.3}, {"modify": {"b.c": "int b;\\n"}}],
         "expect": {"exit": 0, "taskCount": 2, "files": ["b.o", "app"]}},
    ], description="fail → fix → только b.o + app"),
]
'''

TESTS_PARALLEL = '''# -*- coding: utf-8 -*-
from ._common import test, pp

TESTS = [
    test("parallel_race", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule noop { command = "${python} touch.py ${out}" }
\'\'\' + "\\n".join(f\'build "r_{i}.txt" : noop\' for i in range(1, 31)) +
\'\\nalias all : \' + ", ".join(f\'"r_{i}.txt"\' for i in range(1, 31)) + "\\n"),
    }, runs=[
        {"name": "parallel_30", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 30,
                    "files": [f"r_{i}.txt" for i in range(1, 31)]}},
    ], description="30 задач параллельно -j 8"),

    test("parallel_speed", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule slow { command = "${python} sleep_and_touch.py 200 ${out}" }
\'\'\' + "\\n".join(f\'build "s_{i}.txt" : slow\' for i in range(1, 9)) +
\'\\nalias all : \' + ", ".join(f\'"s_{i}.txt"\' for i in range(1, 9)) + "\\n"),
    }, runs=[
        {"name": "parallel_fast", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 8,
                    "files": [f"s_{i}.txt" for i in range(1, 9)]}},
    ], description="8 задач × 200ms sleep"),

    test("real_parallel_correctness", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} echo_to.py ${out} MARKER_${marker}" }
\'\'\' + "\\n".join(f\'build "p_{i}.txt" : gen {{ marker = "id_{i}" }}\' for i in range(1, 16)) +
\'\\nalias all : \' + ", ".join(f\'"p_{i}.txt"\' for i in range(1, 16))),
    }, runs=[
        {"name": "j8", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 15,
                    "filesWith": {f"p_{i}.txt": f"MARKER_id_{i}" for i in range(1, 16)}}},
    ], description="15 задач -j 8"),

    test("real_parallel_partial_fail", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule ok { command = "${python} echo_to.py ${out} OK" }
rule fail { command = "${python} fail.py" }
\'\'\' + "\\n".join(f\'build "p_{i}.txt" : ok\' for i in range(1, 6)) +
\'\\nbuild "bad.txt" : fail\' + \'\\nbuild "p_6.txt" : ok\' +
\'\\nalias all : \' + ", ".join(f\'"p_{i}.txt"\' for i in range(1, 7)) + \', "bad.txt"\'),
    }, runs=[
        {"name": "j8_fail", "args": ["-f", "build.pmake", "-j", "8"], "expect": {"exit": 1}},
        {"name": "dry_after", "args": ["-f", "build.pmake", "-n"],
         "expect": {"exit": 0, "taskCountMin": 1, "taskCountMax": 7}},
    ], description="-j 8 fail"),

    test("real_parallel_diamond_j8", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "b.out" : copy "a.in"
build "c.out" : copy "a.in"
build "d.out" : copy "b.out"
build "e.out" : copy "c.out"
build "f.out" : concat "d.out" "e.out"
alias all : "f.out"
\'\'\',
        "a.in": "diamond8\\n",
    }, runs=[
        {"name": "j8", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 5,
                    "files": ["b.out", "c.out", "d.out", "e.out", "f.out"]}},
    ], description="Diamond -j 8"),
]
'''

TESTS_PATHS = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_paths_spaces", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out file.txt" : copy "in file.txt"
alias all : "out file.txt"
\'\'\',
        "in file.txt": "spaces work\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out file.txt"],
                                     "filesWith": {"out file.txt": "spaces work"}}},
    ], description="пробелы в именах файлов"),

    test("real_paths_unicode", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "файл.txt" : copy "исходник.txt"
alias all : "файл.txt"
\'\'\',
        "исходник.txt": "unicode ok\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["файл.txt"],
                                     "filesWith": {"файл.txt": "unicode ok"}}},
    ], description="Unicode в именах файлов"),
]
'''

TESTS_MODULES = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("foreach", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
names = "alpha beta gamma"
rule echo { command = "${python} echo_to.py ${out} ${n}" }
foreach n in ${names} {
    build "out_${n}.txt" : echo
}
alias all : "out_alpha.txt", "out_beta.txt", "out_gamma.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                     "files": ["out_alpha.txt", "out_beta.txt", "out_gamma.txt"]}},
    ], description="foreach: 3 build из списка"),

    test("if_true", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
mode = "release"
rule echo { command = "${python} echo_to.py ${out} ${tag}" }
if mode == "release" {
    tag = "R"
} else {
    tag = "D"
}
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "R"}}},
    ], description="if true → ветка then"),

    test("switch", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
mode = "debug"
switch mode {
case "debug" { rule echo { command = "${python} echo_to.py ${out} DEBUG" } }
case "release" { rule echo { command = "${python} echo_to.py ${out} RELEASE" } }
default { rule echo { command = "${python} echo_to.py ${out} UNKNOWN" } }
}
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "DEBUG"}}},
    ], description="switch/case/default"),

    test("function_call", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
function say(msg) { payload = "${msg}" }
say("hello_func")
rule echo { command = "${python} echo_to.py ${out} ${payload}" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "hello_func"}}},
    ], description="user-function изменяет переменную"),
]
'''

TESTS_BUILTINS = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("builtin_glob", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
files = glob("*.src", ".")
rule echo { command = "${python} echo_to.py ${out} built" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
        "a.src": "x", "b.src": "y",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
    ], description="glob() находит файлы"),

    test("append_flags", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
flags = "A"
flags += "B"
flags += "C"
rule echo { command = "${python} write_all.py ${out} ${flags}" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "A B C"}}},
    ], description="+= конкатенация через пробел"),
]
'''

TESTS_POOL = '''# -*- coding: utf-8 -*-
from ._common import test, pp

TESTS = [
    test("real_pool_console_serial", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule console_task { command = "${python} sleep_and_touch.py 350 ${out}" pool = console }
\'\'\' + "\\n".join(f\'build "c_{i}.txt" : console_task\' for i in range(1, 4)) +
\'\\nalias all : \' + ", ".join(f\'"c_{i}.txt"\' for i in range(1, 4))),
    }, runs=[
        {"name": "j8_serialized", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 3,
                    "files": ["c_1.txt", "c_2.txt", "c_3.txt"], "elapsedMin": 0.85}},
    ], description="pool=console сериализует при -j8"),

    test("real_pool_console_with_order", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule console_task { command = "${python} sleep_and_touch.py 300 ${out}" pool = console }
build "stamp.txt" : console_task
build "out.txt" : console_task || "stamp.txt"
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "j8_serialized", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"],
                    "elapsedMin": 0.55}},
    ], description="pool=console + order-only"),
]
'''

TESTS_RSPFILE = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_rspfile_basic", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen {
    command = "${python} cat_rspfile.py ${rspfile} ${out}"
    rspfile = "${out}.rsp"
    rspfile_content = "line1\\\\nline2\\\\n"
}
build "out.txt" : gen
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"],
                                     "filesWith": {"out.txt": "line1\\nline2"}}},
        {"name": "rspfile_removed", "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0, "filesNot": ["out.txt.rsp"]}},
    ], description="rspfile: пишется перед командой, удаляется после"),
]
'''

TESTS_GEN = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_generator_skip", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} touch.py ${out}" generator = true }
build "gen.txt" : gen
alias all : "gen.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["gen.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_output", "before": [{"sleep": 0.3}, {"delete": ["gen.txt"]}],
         "expect": {"exit": 0, "taskCount": 1, "files": ["gen.txt"]}},
    ], description="generator: exists → skip; missing → rebuild"),

    test("real_generator_mtime", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} touch.py ${out}" generator = true }
build "gen.txt" : gen "src.in"
alias all : "gen.txt"
\'\'\',
        "src.in": "src\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["gen.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_src_should_rebuild",
         "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="generator mtime"),
]
'''

TESTS_CMAKE = '''# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_cmake_full_pattern", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule autogen { command = "${python} sleep_and_touch.py 200 ${out}" restat = true }
rule compile { command = "${python} check_stamp.py autogen_stamp ${in} ${out}" }
build "autogen_stamp" : autogen "gen.in"
build "cmake_obj_deps" : phony || "autogen_stamp"
build "main.o" : compile "main.c" || "cmake_obj_deps"
build "app" : compile "main.o" || "cmake_obj_deps"
alias all : "app"
\'\'\',
        "gen.in": "gen\\n", "main.c": "main\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                     "files": ["autogen_stamp", "main.o", "app"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_gen_order_only_no_propagate",
         "before": [{"sleep": 0.3}, {"touch": ["gen.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "touch_main_c_rebuild",
         "before": [{"sleep": 0.3}, {"touch": ["main.c"]}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="CMake full pattern"),
]
'''

TESTS_MISC = '''# -*- coding: utf-8 -*-
from ._common import test, pp, SHELL_PREFIX

TESTS = [
    test("command_shell", {
        "build.pmake": pp(f\'\'\'pmake_required_version = 1.0
rule echo_chain {{
    command = "{SHELL_PREFIX} \\\\"echo A > a.txt && echo B > b.txt && echo C > out.txt\\\\""
}}
build "out.txt" : echo_chain
alias all : "out.txt"
\'\'\'),
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "files": ["a.txt", "b.txt", "out.txt"],
                                     "filesWith": {"a.txt": "A", "b.txt": "B", "out.txt": "C"}}},
    ], description="shell && через {SHELL_PREFIX}"),

    test("dry_run", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} never" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "dry", "args": ["-f", "build.pmake", "-n"],
         "expect": {"exit": 0, "taskCount": 1, "filesNot": ["out.txt"]}},
    ], description="-n не создаёт файлы"),

    test("clean", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "build_first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
        {"name": "clean_cmd", "args": ["clean", "-f", "build.pmake"],
         "expect": {"exit": 0, "filesNot": ["out.txt"]}},
    ], description="pmake clean удаляет outputs"),

    test("cache_delete", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out.c" : copy "src.c"
alias all : "out.c"
\'\'\',
        "src.c": "int v = 1;\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.c"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_cache_noop",
         "before": [{"sleep": 0.5}, {"delete": ["Project-cache.pmake"]}],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_cache_and_modify",
         "before": [{"sleep": 0.5}, {"delete": ["Project-cache.pmake"]},
                    {"modify": {"src.c": "int v = 2;\\n"}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.c": "int v = 2;"}}},
    ], description="Кэш — ускоритель: удаление не форсит rebuild"),

    test("real_partial_then_dry_run", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule ok { command = "${python} echo_to.py ${out} OK" }
rule fail { command = "${python} fail.py" }
build "a.txt" : ok
build "b.txt" : fail "a.txt"
build "c.txt" : ok "b.txt"
alias all : "c.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 1, "files": ["a.txt"]}},
        {"name": "dry", "args": ["-f", "build.pmake", "-n"],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="fail: -n показывает b + c"),

    test("real_cache_saved_before_build", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule fail { command = "${python} fail.py" }
build "out.txt" : fail
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first_fail", "expect": {"exit": 1}},
        {"name": "cache_exists", "args": ["-f", "build.pmake", "-n"],
         "expect": {"exit": 0, "files": ["Project-cache.pmake"]}},
    ], description="fail: кэш до сборки"),

    test("real_multi_output", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} multi_out.py ${out}" }
build "a.txt", "b.txt", "c.txt" : gen
alias all : "a.txt", "b.txt", "c.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["a.txt", "b.txt", "c.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_b", "before": [{"sleep": 0.3}, {"delete": ["b.txt"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="multi-output"),

    test("real_wide_fanout", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
\'\'\' + "\\n".join(f\'build "w_{i}.out" : copy "src.in"\' for i in range(1, 21)) +
\'\\nalias all : \' + ", ".join(f\'"w_{i}.out"\' for i in range(1, 21))),
        "src.in": "fanout\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 20}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 20}},
    ], description="1 → 20 fanout"),

    test("real_new_target", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
alias all : "a.out"
\'\'\'),
        "a.in": "new_target_a\\n", "b.in": "new_target_b\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["a.out"]}},
        {"name": "add_b_target",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
build "b.out" : copy "b.in"
alias all : "a.out", "b.out"
\'\'\')}}],
         "expect": {"exit": 0, "taskCount": 1, "files": ["b.out"]}},
    ], description="добавили build → только новый пересобирается"),

    test("real_build_file_comment", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "add_comment",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp(\'\'\'pmake_required_version = 1.0
// just a comment
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\')}}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="комментарий не форсит rebuild"),

    test("real_sanity_unknown_target", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
\'\'\',
    }, runs=[
        {"name": "unknown", "args": ["-f", "build.pmake", "nonexistent_target"],
         "expect": {"exit": 1}},
    ], description="pmake nonexistent_target → exit 1"),

    test("real_version", {
        "build.pmake": "pmake_required_version = 1.0\\nalias all :\\n",
    }, runs=[
        {"name": "version", "args": ["-V"],
         "expect": {"exit": 0, "stdoutMatches": [r"\\d+\\.\\d+"]}},
    ], description="pmake -V"),

    test("real_help", {
        "build.pmake": "pmake_required_version = 1.0\\nalias all :\\n",
    }, runs=[
        {"name": "help", "args": ["--help"],
         "expect": {"exit": 0, "stdoutMatches": ["(?i)usage|option"]}},
    ], description="pmake --help"),

    test("real_no_build_file", {}, runs=[
        {"name": "no_file", "args": ["-f", "build.pmake"],
         "expect": {"exit": 1, "stderrMatches": ["(?i)cannot read|no build file|build\\\\.pmake"]}},
    ], description="нет build.pmake"),
]
'''

TESTS_HARD = '''# -*- coding: utf-8 -*-
from ._common import test, pp


def _rsp(n=100):
    files = {f"in_{i}.txt": f"content_{i}\\n" for i in range(1, n + 1)}
    inputs = " ".join(f\'"in_{i}.txt"\' for i in range(1, n + 1))
    files["build.pmake"] = (
        \'pmake_required_version = 1.0\\n\'
        \'python = "@PYTHON@"\\n\'
        \'rule link_all {\\n\'
        \'    command = "${python} cat_rspfile.py ${rspfile} ${out}"\\n\'
        \'    rspfile = "${out}.rsp"\\n\'
        \'    rspfile_content = "${in}"\\n\'
        \'}\\n\'
        f\'build "out.txt" : link_all {inputs}\\n\'
        \'alias all : "out.txt"\\n\'
    )
    expected = " ".join(f"in_{i}.txt" for i in range(1, n + 1))
    return files, n, expected


_rsp_files, _rsp_n, _rsp_expected = _rsp(100)


def _stress(n=200):
    lines = [
        \'pmake_required_version = 1.0\',
        \'python = "@PYTHON@"\',
        \'rule touch { command = "${python} touch.py ${out}" }\',
    ]
    for i in range(1, n + 1):
        lines.append(f\'build "s_{i}.txt" : touch\')
    lines.append(\'alias all : \' + ", ".join(f\'"s_{i}.txt"\' for i in range(1, n + 1)))
    return {"build.pmake": "\\n".join(lines) + "\\n"}, n


_stress_files, _stress_n = _stress(200)

TESTS = [
    test("real_cache_after_failure", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_maybe_fail.py ${in} ${out}" }
rule link { command = "${python} concat.py ${out} ${in}" }
\'\'\' + "\\n".join(f\'build "f_{i}.o" : cc "f_{i}.c"\' for i in range(1, 31)) +
\'\\nbuild "app" : link \' + " ".join(f\'"f_{i}.o"\' for i in range(1, 31)) +
\'\\nalias all : "app"\'),
        **{f"f_{i}.c": ("FAIL\\n" if i == 15 else f"int v_{i};\\n") for i in range(1, 31)},
    }, runs=[
        {"name": "first_fail", "args": ["-f", "build.pmake", "-j", "1"],
         "expect": {"exit": 1, "files": [f"f_{i}.o" for i in range(1, 15)],
                    "filesNot": ["f_15.o", "app"]}},
        {"name": "fix_and_retry",
         "before": [{"sleep": 0.3}, {"modify": {"f_15.c": "int v_15;\\n"}}],
         "args": ["-f", "build.pmake", "-j", "1"],
         "expect": {"exit": 0, "files": [f"f_{i}.o" for i in range(1, 31)] + ["app"]}},
    ], description="30 files, f_15 fails → fix", timeout=120),

    test("real_rspfile_large", _rsp_files, runs=[
        {"name": "build_large_rsp",
         "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"],
                    "filesWith": {"out.txt": _rsp_expected}}},
    ], description=f"rspfile with {_rsp_n} inputs"),

    test("real_parallel_stress", _stress_files, runs=[
        {"name": "j16_stress", "args": ["-f", "build.pmake", "-j", "16"],
         "expect": {"exit": 0, "taskCount": 200,
                    "files": [f"s_{i}.txt" for i in range(1, 201)]}},
    ], description="200 tasks -j16 stress", timeout=180),

    test("real_truncated_cache", {
        "build.pmake": \'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "a.out" : copy "a.in"
build "b.out" : copy "b.in"
build "app" : concat "a.out" "b.out"
alias all : "app"
\'\'\',
        "a.in": "A\\n", "b.in": "B\\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3, "files": ["a.out", "b.out", "app"]}},
        {"name": "corrupt_cache",
         "before": [{"sleep": 0.3}, {"modify": {"Project-cache.pmake": "GARBAGE_CORRUPTED"}}],
         "expect": {"exit": 0, "taskCountMax": 3,
                    "files": ["a.out", "b.out", "app"]}},
        {"name": "recovered_noop", "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="truncated cache → no crash"),

    test("real_kill_cleanup", {
        "build.pmake": pp(\'\'\'pmake_required_version = 1.0
python = "@PYTHON@"
rule slow { command = "${python} delayed_echo.py 300 ${out} content" }
\'\'\' + "\\n".join(f\'build "k_{i}.txt" : slow\' for i in range(1, 11)) +
\'\\nalias all : \' + ", ".join(f\'"k_{i}.txt"\' for i in range(1, 11))),
    }, runs=[
        {"name": "kill_during", "args": ["-f", "build.pmake", "-j", "2"],
         "kill_after_ms": 700, "continue": True, "expect": {}},
        {"name": "resume_after_kill", "before": [{"sleep": 0.5}],
         "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "files": [f"k_{i}.txt" for i in range(1, 11)]}},
    ], description="Ctrl+C mid-build → restart completes", timeout=90),
]
'''

# Регистрация модулей тестов
FILES["src/tests_basic.py"] = TESTS_BASIC
FILES["src/tests_incremental.py"] = TESTS_INCR
FILES["src/tests_restat.py"] = TESTS_RESTAT
FILES["src/tests_order_only.py"] = TESTS_ORDER
FILES["src/tests_phony.py"] = TESTS_PHONY
FILES["src/tests_depfile.py"] = TESTS_DEPFILE
FILES["src/tests_command_hash.py"] = TESTS_HASH
FILES["src/tests_include.py"] = TESTS_INCLUDE
FILES["src/tests_errors.py"] = TESTS_ERRORS
FILES["src/tests_parallel.py"] = TESTS_PARALLEL
FILES["src/tests_paths.py"] = TESTS_PATHS
FILES["src/tests_modules.py"] = TESTS_MODULES
FILES["src/tests_builtins.py"] = TESTS_BUILTINS
FILES["src/tests_pool.py"] = TESTS_POOL
FILES["src/tests_rspfile.py"] = TESTS_RSPFILE
FILES["src/tests_generator.py"] = TESTS_GEN
FILES["src/tests_cmake.py"] = TESTS_CMAKE
FILES["src/tests_misc.py"] = TESTS_MISC
FILES["src/tests_hard.py"] = TESTS_HARD


# ============================================================
# MAIN
# ============================================================
def main():
    root = Path.cwd()
    (root / "src").mkdir(exist_ok=True)

    for rel, content in FILES.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content, encoding="utf-8")

    total = 3 + 9 + 9 + 9 + 4 + 4 + 3 + 3 + 4 + 5 + 2 + 4 + 2 + 2 + 1 + 2 + 1 + 14 + 5

    print()
    print("=" * 72)
    print(f" [OK] Setup complete: {root}")
    print("=" * 72)
    print(f" Total tests: {total}")
    print()
    print(" Структура:")
    print("   run.py")
    print("   build.pmake")
    print("   src/")
    for rel in sorted(FILES):
        if rel.startswith("src/") and rel != "src/__init__.py":
            print(f"     {rel[4:]}")
    print()
    print(" Модули по категориям:")
    print("   tests_basic.py          (3)")
    print("   tests_incremental.py    (9)")
    print("   tests_restat.py         (9)")
    print("   tests_order_only.py     (9)")
    print("   tests_phony.py          (4)")
    print("   tests_depfile.py        (4)")
    print("   tests_command_hash.py   (3)")
    print("   tests_include.py        (3)")
    print("   tests_errors.py         (4)")
    print("   tests_parallel.py       (5)")
    print("   tests_paths.py          (2)")
    print("   tests_modules.py        (4)")
    print("   tests_builtins.py       (2)")
    print("   tests_pool.py           (2)")
    print("   tests_rspfile.py        (1)")
    print("   tests_generator.py      (2)")
    print("   tests_cmake.py          (1)")
    print("   tests_misc.py           (14)")
    print("   tests_hard.py           (5)")
    print()
    print(" Запуск:")
    print(f'   python run.py --pmake pmake.exe')
    print(f'   python run.py --pmake pmake.exe --filter "real_restat*"')
    print()
    print(" Через pmake (генерирует .generated):")
    print(f'   pmake')
    print("=" * 72)


if __name__ == "__main__":
    main()