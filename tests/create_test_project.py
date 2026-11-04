# -*- coding: utf-8 -*-
"""
create_test_project.py — создаёт PMAKE test project в TEST/ с полноценным
набором тестов. Запятые в alias — обязательны (дизайн парсера pmake).
"""
from __future__ import annotations

import sys as _sys
try:
    _sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    _sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import sys          # для sys.exit в main() (был NameError)
import platform
import argparse
import json
import shutil
from pathlib import Path

DEFAULT_DIR = r"C:\Users\abubakir\Desktop\TEST"
REAL_PYTHON = "D:/bin/python3.exe"

IS_WINDOWS = platform.system() == "Windows"
SHELL_PREFIX = "cmd /c" if IS_WINDOWS else "sh -c"


# ============================================================
# HELPERS — Python-скрипты для build.pmake правил
# ============================================================
HELPERS = {
    "touch.py": '''
import sys, pathlib
for arg in sys.argv[1:]:
    p = pathlib.Path(arg)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.touch()
''',
    "copy.py": '''
import sys, shutil, pathlib
if len(sys.argv) != 3:
    sys.exit("usage: copy.py SRC DST")
pathlib.Path(sys.argv[2]).parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(sys.argv[1], sys.argv[2])
''',
    "echo_to.py": '''
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(sys.argv[2] + "\\n", encoding="utf-8")
''',
    "write_all.py": '''
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(" ".join(sys.argv[2:]) + "\\n", encoding="utf-8")
''',
    "concat.py": '''
import sys, pathlib
out = pathlib.Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("".join(pathlib.Path(p).read_text(encoding="utf-8") for p in sys.argv[2:]), encoding="utf-8")
''',
    "make_dep.py": '''
import sys, pathlib
out = sys.argv[1]
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
header = sys.argv[2] if len(sys.argv) > 2 else ""
if header:
    pathlib.Path(out + ".d").write_text(f"{out}: {header}\\n", encoding="utf-8")
''',
    "restat.py": '''
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.touch()
p.write_text("constant\\n", encoding="utf-8")
''',
    "fail.py": '''
import sys
print("intentional failure", file=sys.stderr)
sys.exit(1)
''',
    "sleep.py": '''
import sys, time
time.sleep(int(sys.argv[1]) / 1000.0)
''',
    "sleep_and_touch.py": '''
import sys, time, pathlib
time.sleep(int(sys.argv[1]) / 1000.0)
p = pathlib.Path(sys.argv[2])
p.parent.mkdir(parents=True, exist_ok=True)
p.touch()
''',
}


# ============================================================
# TEST HELPERS
# ============================================================
TESTS: list = []


def pp(s: str) -> str:
    """Подставить @PYTHON@ → реальный путь."""
    return s.replace("@PYTHON@", REAL_PYTHON)


def test(name: str, files: dict, runs: list, description: str = "",
         bug: str = "", fix: str = "", args: list = None, timeout: int = 30):
    TESTS.append({
        "name": name,
        "files": {k: pp(v) for k, v in files.items()},
        "meta": {
            "description": description,
            "bug": bug,
            "fix_hint": fix,
            "args": args or ["-f", "build.pmake"],
            "timeoutSec": timeout,
            "runs": runs,
        },
    })


# ============================================================
# TESTS
# ============================================================

# ---------- BASELINE ----------
test("basic", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo {
    command = "${python} echo_to.py ${out} hello"
    description = "ECHO ${out}"
}
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["out.txt"], "filesWith": {"out.txt": "hello"}}},
    {"name": "noop", "before": [{"sleep": 0.5}],
     "expect": {"exit": 0, "taskCount": 0}},
], description="Минимум: одно правило, один build, alias")


test("two_builds", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo {
    command = "${python} echo_to.py ${out} x"
}
build "a.txt" : echo
build "b.txt" : echo
alias all : "a.txt", "b.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2,
                                 "files": ["a.txt", "b.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.5}],
     "expect": {"exit": 0, "taskCount": 0}},
], description="Два независимых build")


test("alias_multi", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo {
    command = "${python} echo_to.py ${out} y"
}
build "x.txt" : echo
build "y.txt" : echo
build "z.txt" : echo
alias all : "x.txt", "y.txt", "z.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                 "files": ["x.txt", "y.txt", "z.txt"]}},
], description="alias с 3 targets (comma-separated)")


# ---------- INCREMENTAL ----------
test("incr_source", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy {
    command = "${python} copy.py ${in} ${out}"
    description = "COPY ${in} -> ${out}"
}
build "out.c" : copy "src.c"
alias all : "out.c"
''',
    "src.c": "int v = 1;\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.c"]}},
    {"name": "noop", "before": [{"sleep": 0.5}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "modify_src", "before": [{"sleep": 0.5}, {"modify": {"src.c": "int v = 2;\n"}}],
     "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.c": "int v = 2;"}}},
], description="Изменение src.c → out.c пересобирается с новым содержимым")


test("incr_delete_output", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo {
    command = "${python} echo_to.py ${out} z"
}
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
    {"name": "delete_and_rebuild",
     "before": [{"sleep": 0.5}, {"delete": ["out.txt"]}],
     "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
], description="Удаление output → пересборка")


test("incr_chain", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy {
    command = "${python} copy.py ${in} ${out}"
}
build "a.out" : copy "a.in"
build "b.out" : copy "a.out"
build "c.out" : copy "b.out"
alias all : "c.out"
''',
    "a.in": "deep\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                 "files": ["a.out", "b.out", "c.out"]}},
    {"name": "noop", "before": [{"sleep": 0.5}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "touch_a_in", "before": [{"sleep": 0.5}, {"touch": ["a.in"]}],
     "expect": {"exit": 0, "taskCountMin": 3, "taskCountMax": 3}},
], description="a.in → a.out → b.out → c.out; touch a.in → вся цепочка")


# ---------- PARALLEL ----------
test("parallel_race", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule noop {
    command = "${python} touch.py ${out}"
    description = "NOOP ${out}"
}
''' + "\n".join(f'build "r_{i}.txt" : noop' for i in range(1, 31)) +
'\nalias all : ' + ", ".join(f'"r_{i}.txt"' for i in range(1, 31)) + "\n"),
}, runs=[
    {"name": "parallel_30", "args": ["-f", "build.pmake", "-j", "8"],
     "expect": {"exit": 0, "taskCount": 30,
                "files": [f"r_{i}.txt" for i in range(1, 31)]}},
], description="30 задач параллельно -j 8 — проверка гонок")


test("parallel_speed", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule slow {
    command = "${python} sleep_and_touch.py 200 ${out}"
}
''' + "\n".join(f'build "s_{i}.txt" : slow' for i in range(1, 9)) +
'\nalias all : ' + ", ".join(f'"s_{i}.txt"' for i in range(1, 9)) + "\n"),
}, runs=[
    {"name": "parallel_fast", "args": ["-f", "build.pmake", "-j", "8"],
     "expect": {"exit": 0, "taskCount": 8,
                "files": [f"s_{i}.txt" for i in range(1, 9)]}},
], description="8 задач × 200ms sleep, -j 8 → ускорение (sanity)")


# ---------- SHELL (cmd /c или sh -c + &&) ----------
test("command_shell", {
    "build.pmake": pp(f'''pmake_required_version = 1.0
rule echo_chain {{
    command = "{SHELL_PREFIX} \\"echo A > a.txt && echo B > b.txt && echo C > out.txt\\""
}}
build "out.txt" : echo_chain
alias all : "out.txt"
'''),
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["a.txt", "b.txt", "out.txt"],
                                 "filesWith": {"a.txt": "A",
                                               "b.txt": "B",
                                               "out.txt": "C"}}},
], description="shell && через {SHELL_PREFIX} — три echo, все три файла созданы")


# ---------- DEPFILE ----------
test("depfile_basic", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule mk {
    command = "${python} make_dep.py ${out} hdr.h"
    description = "MK ${out}"
    depfile = "${out}.d"
    deps = "gcc"
}
build "out.o" : mk "hdr.h"
alias all : "out.o"
''',
    "hdr.h": "/* v1 */\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.o"]}},
    {"name": "noop", "before": [{"sleep": 0.5}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "touch_header", "before": [{"sleep": 0.5}, {"touch": ["hdr.h"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="depfile: touch заголовка → пересборка")


test("depfile_unrelated", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule mk {
    command = "${python} make_dep.py ${out} hdr.h"
    depfile = "${out}.d"
    deps = "gcc"
}
build "out.o" : mk "hdr.h"
alias all : "out.o"
''',
    "hdr.h": "/* v1 */\n",
    "other.h": "/* unrelated */\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
    {"name": "touch_unrelated",
     "before": [{"sleep": 0.5}, {"touch": ["other.h"]}],
     "expect": {"exit": 0, "taskCount": 0}},
], description="depfile: touch нерелевантного → НЕ пересборка")


# ---------- RESTAT ----------
test("restat", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule touch_always {
    command = "${python} restat.py ${out}"
    description = "RESTAT ${out}"
    restat = true
}
rule copy {
    command = "${python} copy.py ${in} ${out}"
}
build "mid.txt" : touch_always
build "final.txt" : copy "mid.txt"
alias all : "final.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2,
                                 "files": ["mid.txt", "final.txt"]}},
    {"name": "noop_restat", "before": [{"sleep": 0.5}],
     "expect": {"exit": 0, "taskCount": 0},
     "comment": "restat=true + content unchanged → downstream не пересобирается"},
], description="restat=true: файл touch'ится, но контент тот же → final не пересобирается")


# ---------- FOREACH ----------
test("foreach", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
names = "alpha beta gamma"
rule echo {
    command = "${python} echo_to.py ${out} ${n}"
}
foreach n in ${names} {
    build "out_${n}.txt" : echo
}
alias all : "out_alpha.txt", "out_beta.txt", "out_gamma.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                 "files": ["out_alpha.txt", "out_beta.txt", "out_gamma.txt"]}},
], description="foreach: 3 build из списка")


# ---------- IF ----------
test("if_true", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
mode = "release"
rule echo {
    command = "${python} echo_to.py ${out} ${tag}"
}
if mode == "release" {
    tag = "R"
} else {
    tag = "D"
}
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "filesWith": {"out.txt": "R"}}},
], description="if true → ветка then")


# ---------- SWITCH ----------
test("switch", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
mode = "debug"
switch mode {
case "debug" {
    rule echo { command = "${python} echo_to.py ${out} DEBUG" }
}
case "release" {
    rule echo { command = "${python} echo_to.py ${out} RELEASE" }
}
default {
    rule echo { command = "${python} echo_to.py ${out} UNKNOWN" }
}
}
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "filesWith": {"out.txt": "DEBUG"}}},
], description="switch/case/default из документации")


# ---------- FUNCTION ----------
test("function_call", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
function say(msg) {
    payload = "${msg}"
}
say("hello_func")
rule echo {
    command = "${python} echo_to.py ${out} ${payload}"
}
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "filesWith": {"out.txt": "hello_func"}}},
], description="user-function изменяет переменную")


# ---------- INCLUDE ----------
test("include_basic", {
    "build.pmake": '''pmake_required_version = 1.0
include "sub.pmake"
build "out.txt" : echo
alias all : "out.txt"
''',
    "sub.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo {
    command = "${python} echo_to.py ${out} from_sub"
}
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "filesWith": {"out.txt": "from_sub"}}},
], description="include простого файла")


test("include_relative", {
    "build.pmake": '''pmake_required_version = 1.0
include "subdir/part.pmake"
build "out.txt" : echo
alias all : "out.txt"
''',
    "subdir/part.pmake": 'pmake_required_version = 1.0\ninclude "nested.pmake"\n',
    "subdir/nested.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} nested_ok" }
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "filesWith": {"out.txt": "nested_ok"}}},
], description="include с относительным путём от родителя")


test("include_cycle", {
    "build.pmake": '''pmake_required_version = 1.0
include "a.pmake"
''',
    "a.pmake": 'pmake_required_version = 1.0\ninclude "b.pmake"\n',
    "b.pmake": 'pmake_required_version = 1.0\ninclude "a.pmake"\n',
}, runs=[
    {"name": "cycle_detected",
     "expect": {"exit": 1, "stderrHas": ["ycl"]}},
], description="include cycle должен вернуть ошибку, не крашить")


# ---------- BUILTINS ----------
test("builtin_glob", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
files = glob("*.src", ".")
rule echo {
    command = "${python} echo_to.py ${out} built"
}
build "out.txt" : echo
alias all : "out.txt"
''',
    "a.src": "x",
    "b.src": "y",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
], description="glob() находит файлы")


# ---------- APPEND ----------
test("append_flags", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
flags = "A"
flags += "B"
flags += "C"
rule echo {
    command = "${python} write_all.py ${out} ${flags}"
}
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "filesWith": {"out.txt": "A B C"}}},
], description="+= конкатенация через пробел")


# ---------- MODES ----------
test("dry_run", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo {
    command = "${python} echo_to.py ${out} never"
}
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "dry", "args": ["-f", "build.pmake", "-n"],
     "expect": {"exit": 0, "taskCount": 1, "filesNot": ["out.txt"]}},
], description="-n не создаёт файлы")


test("clean", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo {
    command = "${python} echo_to.py ${out} x"
}
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "build_first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
    {"name": "clean_cmd", "args": ["clean", "-f", "build.pmake"],
     "expect": {"exit": 0, "filesNot": ["out.txt"]}},
], description="pmake clean удаляет outputs")


# ---------- ERRORS ----------
test("error_cycle", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a" : copy "b"
build "b" : copy "a"
alias all : "a"
''',
}, runs=[
    {"name": "cycle_should_fail",
     "expect": {"exit": 1, "stderrHas": ["ycl"]}},
], description="цикл в графе → ошибка, не зависание")


test("error_missing_rule", {
    "build.pmake": '''pmake_required_version = 1.0
build "out.txt" : nonexistent_rule
alias all : "out.txt"
''',
}, runs=[
    {"name": "missing_rule_should_fail",
     "expect": {"exit": 1}},
], description="ссылка на несуществующее правило → ошибка")


test("error_bad_syntax", {
    "build.pmake": '''pmake_required_version = 1.0
build "out.txt" :       # забыли имя правила
alias all : "out.txt"
''',
}, runs=[
    {"name": "bad_syntax_should_fail",
     "expect": {"exit": 1}},
], description="синтаксическая ошибка → exit 1")


# ---------- CACHE ----------
test("cache_delete", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy {
    command = "${python} copy.py ${in} ${out}"
}
build "out.c" : copy "src.c"
alias all : "out.c"
''',
    "src.c": "int v = 1;\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.c"]}},
    {"name": "noop", "before": [{"sleep": 0.5}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "delete_cache_noop",
     "before": [{"sleep": 0.5}, {"delete": ["Project-cache.pmake"]}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "delete_cache_and_modify",
     "before": [{"sleep": 0.5},
                {"delete": ["Project-cache.pmake"]},
                {"modify": {"src.c": "int v = 2;\n"}}],
     "expect": {"exit": 0, "taskCount": 1,
                "filesWith": {"out.c": "int v = 2;"}}},
], description="Кэш — ускоритель, не источник истины: удаление не форсит rebuild, "
              "изменение source — форсит")


# ============================================================
# RUNNER (embed как RUN_PY)
# ============================================================
RUN_PY = r'''#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run.py — прогоняет tests/ и пишет результаты в results/."""
import sys
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import argparse
import fnmatch
import json
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
        if p.exists():
            return p.absolute()
        w = shutil.which(hint)
        if w:
            return Path(w).absolute()
        raise SystemExit(f"pmake not found: {hint}")
    w = shutil.which("pmake") or shutil.which("pmake.exe")
    if w:
        return Path(w).absolute()
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
        if "sleep" in a:
            time.sleep(a["sleep"])
        elif "touch" in a:
            for path in a["touch"]:
                p = cwd / path
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
        elif "delete" in a:
            for path in a["delete"]:
                p = cwd / path
                if p.exists():
                    p.unlink()
        elif "modify" in a:
            for path, content in a["modify"].items():
                p = cwd / path
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(content, encoding="utf-8")


def parse_task_count(stdout):
    n = 0
    for line in stdout.splitlines():
        s = line.strip()
        if not s.startswith("["): continue
        end = s.find("]")
        if end < 2: continue
        inner = s[1:end]
        if "/" not in inner: continue
        parts = inner.split("/")
        if len(parts) == 2 and parts[0].strip().isdigit() and parts[1].strip().isdigit():
            n += 1
    return n


def check_expect(expect, result, cwd):
    fails = []
    if "exit" in expect and result["exit"] != expect["exit"]:
        fails.append(f"exit={result['exit']}, expected {expect['exit']}")
    if result.get("timedOut"):
        fails.append("TIMEOUT")
        return fails
    tc = parse_task_count(result["stdout"])
    if "taskCount" in expect and tc != expect["taskCount"]:
        fails.append(f"taskCount={tc}, expected {expect['taskCount']}")
    if "taskCountMin" in expect and tc < expect["taskCountMin"]:
        fails.append(f"taskCount={tc} < min {expect['taskCountMin']}")
    if "taskCountMax" in expect and tc > expect["taskCountMax"]:
        fails.append(f"taskCount={tc} > max {expect['taskCountMax']}")
    for s in expect.get("stdoutHas", []):
        if s not in result["stdout"]:
            fails.append(f"stdout missing: {s!r}")
    for s in expect.get("stdoutNo", []):
        if s in result["stdout"]:
            fails.append(f"stdout should NOT contain: {s!r}")
    for s in expect.get("stderrHas", []):
        if s not in result["stderr"]:
            fails.append(f"stderr missing: {s!r}")
    for s in expect.get("stderrNo", []):
        if s in result["stderr"]:
            fails.append(f"stderr should NOT contain: {s!r}")
    for f in expect.get("files", []):
        if not (cwd / f).exists():
            fails.append(f"missing file: {f}")
    for f in expect.get("filesNot", []):
        if (cwd / f).exists():
            fails.append(f"unexpected file: {f}")
    for f, content in expect.get("filesWith", {}).items():
        p = cwd / f
        if not p.exists():
            fails.append(f"missing file (with content): {f}")
            continue
        try:
            actual = p.read_text(encoding="utf-8", errors="replace").strip()
            exp = content.strip()
            if actual != exp:
                fails.append(f"file {f}: expected {exp!r}, got {actual[:200]!r}")
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
        if f.name == "meta.json":
            continue
        if f.is_file():
            shutil.copy2(f, work / f.name)
        elif f.is_dir():
            shutil.copytree(f, work / f.name)

    for h in helpers_dir.iterdir():
        if h.is_file():
            shutil.copy2(h, work / h.name)

    timeout = int(meta.get("timeoutSec", 30))
    default_args = meta.get("args", ["-f", "build.pmake"])

    logs = []
    all_fails = []
    total_elapsed = 0.0

    for run in meta.get("runs", []):
        run_name = run.get("name", "run")
        try:
            apply_actions(run.get("before", []), work)
        except Exception as e:
            all_fails.append(f"[{run_name}] before: {type(e).__name__}: {e}")
            break

        args = run.get("args", default_args)
        result = run_pmake(pmake, args, work, timeout)
        total_elapsed += result["elapsed"]

        logs.append(f"=== {run_name} ===\n"
                    f"args: {args}\n"
                    f"exit: {result['exit']}\n"
                    f"elapsed: {result['elapsed']:.2f}s\n\n"
                    f"--- STDOUT ---\n{result['stdout']}\n"
                    f"--- STDERR ---\n{result['stderr']}\n")

        fails = check_expect(run.get("expect", {}), result, work)
        if fails:
            all_fails.extend([f"[{run_name}] {x}" for x in fails])
            if not run.get("continue", False):
                break

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    log_path = RESULTS_DIR / f"{name}.log"
    header = f"=== TEST: {name} ===\n"
    if meta.get("description"):
        header += f"Description: {meta['description']}\n"
    if meta.get("bug"):
        header += f"Known bug: {meta['bug']}\n"
    if meta.get("fix_hint"):
        header += f"Fix hint: {meta['fix_hint']}\n"
    header += "\n"
    log_path.write_text(header + "\n".join(logs), encoding="utf-8")

    if not keep_temp:
        shutil.rmtree(temp_root, ignore_errors=True)

    return {
        "name": name,
        "status": "PASS" if not all_fails else "FAIL",
        "failures": all_fails,
        "elapsed": total_elapsed,
        "meta": meta,
        "log": str(log_path),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pmake", help="path to pmake.exe")
    ap.add_argument("--filter", default="*", help="fnmatch на имя теста")
    ap.add_argument("--keep-temp", action="store_true")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    pmake = find_pmake(args.pmake)
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    for f in RESULTS_DIR.glob("*.log"):
        f.unlink()
    for f in RESULTS_DIR.glob("*.json"):
        f.unlink()

    test_dirs = sorted(d for d in TESTS_DIR.iterdir()
                       if d.is_dir() and (d / "meta.json").exists())
    if args.filter != "*":
        test_dirs = [d for d in test_dirs if fnmatch.fnmatch(d.name, args.filter)]

    print("=" * 72)
    print(f" PMAKE TEST SUITE ({len(test_dirs)} tests)")
    print(f" pmake: {pmake}")
    print("=" * 72)
    print()

    results = []
    for td in test_dirs:
        r = run_test(pmake, td, HELPERS_DIR, args.keep_temp)
        results.append(r)

        if r["status"] == "PASS":
            print(f"[PASS] {r['name']:<22} ({r['elapsed']:>6.2f}s)")
            if args.verbose:
                for line in (r["meta"].get("description") or "").split("\n"):
                    print(f"       {line}")
        elif r["meta"].get("bug"):
            print(f"[BUG ] {r['name']:<22} ({r['elapsed']:>6.2f}s)  KNOWN")
            print(f"       bug: {r['meta']['bug']}")
            if r["meta"].get("fix_hint"):
                print(f"       fix: {r['meta']['fix_hint']}")
            for f in r["failures"]:
                print(f"       - {f}")
            print(f"       log: {r['log']}")
        else:
            print(f"[FAIL] {r['name']:<22} ({r['elapsed']:>6.2f}s)")
            for f in r["failures"]:
                print(f"       {f}")
            print(f"       log: {r['log']}")

    passed = sum(1 for r in results if r["status"] == "PASS")
    known_bugs = sum(1 for r in results
                     if r["status"] == "FAIL" and r["meta"].get("bug"))
    regressions = sum(1 for r in results
                      if r["status"] == "FAIL" and not r["meta"].get("bug"))

    print()
    print("=" * 72)
    print(f" PASS:        {passed}")
    print(f" KNOWN BUGS:  {known_bugs}   (need fix in pmake)")
    print(f" REGRESSIONS: {regressions}  (something broke!)")
    print("=" * 72)

    report = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "pmake": str(pmake),
        "total": len(results),
        "passed": passed,
        "known_bugs": known_bugs,
        "regressions": regressions,
        "results": [
            {"name": r["name"], "status": r["status"],
             "elapsed": r["elapsed"], "failures": r["failures"],
             "description": r["meta"].get("description", ""),
             "bug": r["meta"].get("bug", ""),
             "fix_hint": r["meta"].get("fix_hint", ""),
             "log": r["log"]}
            for r in results
        ],
    }
    (RESULTS_DIR / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n Report: {RESULTS_DIR / 'report.json'}")

    sys.exit(0 if regressions == 0 and known_bugs == 0 else 1)


if __name__ == "__main__":
    main()
'''


# ============================================================
# MAIN
# ============================================================
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=DEFAULT_DIR)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    root = Path(args.dir)
    if root.exists() and any(root.iterdir()) and not args.force:
        print(f"[!] {root} не пустой. Используй --force.")
        sys.exit(1)

    for old in ["tests", "helpers", "results", "project"]:
        p = root / old
        if p.exists():
            shutil.rmtree(p)

    tests_dir = root / "tests"
    helpers_dir = root / "helpers"
    results_dir = root / "results"
    for d in [tests_dir, helpers_dir, results_dir]:
        d.mkdir(parents=True, exist_ok=True)

    for name, content in HELPERS.items():
        (helpers_dir / name).write_text(content.lstrip(), encoding="utf-8")

    for t in TESTS:
        td = tests_dir / t["name"]
        td.mkdir(parents=True, exist_ok=True)
        for rel, content in t["files"].items():
            f = td / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text(content, encoding="utf-8")
        (td / "meta.json").write_text(
            json.dumps(t["meta"], indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8")

    (root / "run.py").write_text(RUN_PY.lstrip(), encoding="utf-8")

    print()
    print("=" * 72)
    print(f" [OK] Test suite created: {root}")
    print("=" * 72)
    print(f" Tests: {len(TESTS)}")
    print(f" Helpers: {len(HELPERS)}")
    print(f" Python: {REAL_PYTHON}")
    print(f" Shell:  {SHELL_PREFIX}")
    print()
    print(" Categories:")
    cats = {
        "BASELINE": ["basic", "two_builds", "alias_multi"],
        "INCREMENTAL": ["incr_source", "incr_delete_output", "incr_chain"],
        "PARALLEL": ["parallel_race", "parallel_speed"],
        "SHELL": ["command_shell"],
        "DEPFILE": ["depfile_basic", "depfile_unrelated"],
        "RESTAT": ["restat"],
        "FEATURES": ["foreach", "if_true", "switch", "function_call"],
        "INCLUDE": ["include_basic", "include_relative", "include_cycle"],
        "BUILTINS": ["builtin_glob", "append_flags"],
        "MODES": ["dry_run", "clean"],
        "ERRORS": ["error_cycle", "error_missing_rule", "error_bad_syntax"],
        "CACHE": ["cache_delete"],
    }
    for cat, names in cats.items():
        print(f"   {cat:<14} : {', '.join(names)}")
    print()
    print(" Run all:")
    print(f'   {REAL_PYTHON} "{root / "run.py"}" --pmake <path/to/pmake.exe>')
    print()
    print(" Run one:")
    print(f'   {REAL_PYTHON} "{root / "run.py"}" --pmake <path/to/pmake.exe> --filter basic')
    print("=" * 72)


if __name__ == "__main__":
    main()