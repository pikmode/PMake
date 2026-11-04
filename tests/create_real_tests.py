# -*- coding: utf-8 -*-
"""create_real_tests.py — интеграционные тесты pmake (исправленная версия)."""
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
# HELPERS
# ============================================================
HELPERS = {
    "touch.py": '''
import sys, pathlib
for arg in sys.argv[1:]:
    p = pathlib.Path(arg)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.touch()
''',
    "echo_to.py": '''
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(sys.argv[2] + "\\n", encoding="utf-8")
''',
    "copy.py": '''
import sys, shutil, pathlib
if len(sys.argv) != 3:
    sys.exit("usage: copy.py SRC DST")
pathlib.Path(sys.argv[2]).parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(sys.argv[1], sys.argv[2])
''',
    # ИСПРАВЛЕНО: при MISMATCH mtime НЕ восстанавливается —
    # restat должен видеть реальное изменение и propagates.
    # В SKIP-ветке — write тех же байт + точное восстановление mtime
    # (на случай, если pmake удаляет выход перед командой).
    "copy_preserve.py": '''
import sys, pathlib, os
src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
dst.parent.mkdir(parents=True, exist_ok=True)
content = src.read_bytes()
if dst.exists():
    st = dst.stat()
    old = dst.read_bytes()
    if old == content:
        # SKIP: те же байты, mtime сохраняется (перезапись тех же байт + utime)
        dst.write_bytes(content)
        os.utime(dst, ns=(st.st_atime_ns, st.st_mtime_ns))
        sys.exit(0)
    # MISMATCH: пишем, mtime МЕНЯЕТСЯ — restat корректно propagates
    dst.write_bytes(content)
else:
    dst.write_bytes(content)
''',
    # Multi-output + preserve: пишет все outputs, восстанавливает mtime у тех,
    # что не изменились. Используется в real_restat_multi_output.
    "copy_preserve_multi.py": '''
import sys, pathlib, os
src = pathlib.Path(sys.argv[1])
outputs = sys.argv[2:]
content = src.read_bytes()
for out_s in outputs:
    out = pathlib.Path(out_s)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        st = out.stat()
        old = out.read_bytes()
        if old == content:
            out.write_bytes(content)
            os.utime(out, ns=(st.st_atime_ns, st.st_mtime_ns))
            continue
    out.write_bytes(content)
''',
    "cc_make_dep.py": '''
import sys, pathlib
src, out = sys.argv[1], sys.argv[2]
hdr = sys.argv[3] if len(sys.argv) > 3 else ""
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
if hdr:
    pathlib.Path(out + ".d").write_text(f"{out}: {src} {hdr}\\n", encoding="utf-8")
''',
    "cc_maybe_fail.py": '''
import sys, pathlib, time
src = pathlib.Path(sys.argv[1])
out = sys.argv[2]
if src.exists() and "FAIL" in src.read_text(encoding="utf-8"):
    time.sleep(0.2)
    print(f"intentional failure for {out}", file=sys.stderr)
    sys.exit(1)
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
pathlib.Path(out + ".d").write_text(f"{out}: {src.name} common.h\\n", encoding="utf-8")
''',
    "concat.py": '''
import sys, pathlib
out = pathlib.Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("".join(pathlib.Path(p).read_text(encoding="utf-8") for p in sys.argv[2:]), encoding="utf-8")
''',
    "multi_out.py": '''
import sys, pathlib
for out in sys.argv[1:]:
    p = pathlib.Path(out)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(f"from_multi_out_{p.name}\\n", encoding="utf-8")
''',
    "fail.py": '''
import sys
print("intentional failure", file=sys.stderr)
sys.exit(1)
''',
}

TESTS_REAL = []


def pp(s: str) -> str:
    return s.replace("@PYTHON@", REAL_PYTHON)


def test(name, files, runs, description="", bug="", timeout=60):
    TESTS_REAL.append({
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
# 1. INCREMENTAL
# ============================================================
test("real_incr_chain_5", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
build "b.out" : copy "a.out"
build "c.out" : copy "b.out"
build "d.out" : copy "c.out"
build "e.out" : copy "d.out"
alias all : "e.out"
''',
    "a.in": "deep5\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 5}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "touch_a", "before": [{"sleep": 0.3}, {"touch": ["a.in"]}],
     "expect": {"exit": 0, "taskCount": 5}},
], description="5 уровней")


test("real_incr_diamond", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy   { command = "${python} copy.py ${in} ${out}" }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "b.out" : copy "a.in"
build "c.out" : copy "a.in"
build "d.out" : copy "b.out"
build "e.out" : copy "c.out"
build "f.out" : concat "d.out" "e.out"
alias all : "f.out"
''',
    "a.in": "diamond\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 5}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "touch_root", "before": [{"sleep": 0.3}, {"touch": ["a.in"]}],
     "expect": {"exit": 0, "taskCount": 5}},
], description="Diamond граф")


test("real_incr_two_branches", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy   { command = "${python} copy.py ${in} ${out}" }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "x.o" : copy "x.c"
build "y.o" : copy "y.c"
build "app" : concat "x.o" "y.o"
alias all : "app"
''',
    "x.c": "x\n",
    "y.c": "y\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 3}},
    {"name": "touch_x", "before": [{"sleep": 0.3}, {"touch": ["x.c"]}],
     "expect": {"exit": 0, "taskCount": 2}},
    {"name": "touch_y", "before": [{"sleep": 0.3}, {"touch": ["y.c"]}],
     "expect": {"exit": 0, "taskCount": 2}},
], description="Две ветки")


test("real_incr_cpp_header", {
    "build.pmake": '''pmake_required_version = 1.0
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
''',
    "main.c": "main\n",
    "util.c": "util\n",
    "common.h": "// v1\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 3}},
    {"name": "touch_h", "before": [{"sleep": 0.3}, {"touch": ["common.h"]}],
     "expect": {"exit": 0, "taskCount": 3}},
    {"name": "touch_main", "before": [{"sleep": 0.3}, {"touch": ["main.c"]}],
     "expect": {"exit": 0, "taskCount": 2}},
], description="C++: .h → оба .o + app; main.c → main.o + app")


test("real_incr_partial_delete", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
''' + "\n".join(f'build "o_{i}.out" : copy "o_{i}.in"' for i in range(1, 11)) +
'\nalias all : ' + ", ".join(f'"o_{i}.out"' for i in range(1, 11))),
    **{f"o_{i}.in": f"data_{i}\n" for i in range(1, 11)},
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 10}},
    {"name": "delete_3", "before": [{"sleep": 0.3},
                                    {"delete": ["o_3.out", "o_7.out", "o_9.out"]}],
     "expect": {"exit": 0, "taskCount": 3}},
], description="10 → удалили 3 → только 3 пересобираются")


test("real_incr_deep_10", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
''' + "\n".join(f'build "c{i}.out" : copy ' +
                ('"src.in"' if i == 0 else f'"c{i-1}.out"')
                for i in range(0, 10)) +
'\nalias all : "c9.out"'),
    "src.in": "deep10\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 10}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
     "expect": {"exit": 0, "taskCount": 10}},
], description="10-уровневая цепочка")


# ============================================================
# 2. RESTAT
# ============================================================
test("real_restat_skip", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
build "mid.txt" : cp_preserve "src.in"
build "final.txt" : cp_preserve "mid.txt"
alias all : "final.txt"
''',
    "src.in": "constant\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
    {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="restat: touch src → final пропускается (copy_preserve не меняет mtime)")


test("real_restat_chain_3", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
build "m1.txt" : cp_preserve "src.in"
build "m2.txt" : cp_preserve "m1.txt"
build "m3.txt" : cp_preserve "m2.txt"
alias all : "m3.txt"
''',
    "src.in": "chain3\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 3}},
    {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="restat chain 3: touch src → только m1")


test("real_restat_content_change", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
build "mid.txt" : cp_preserve "src.in"
build "final.txt" : cp_preserve "mid.txt"
alias all : "final.txt"
''',
    "src.in": "v1\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
    {"name": "modify_src", "before": [{"sleep": 0.3},
                                      {"modify": {"src.in": "v2\n"}}],
     "expect": {"exit": 0, "taskCount": 2}},
], description="restat: контент изменился → оба пересобираются (mtime меняется)")


test("real_restat_multi_output", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_mp {
    command = "${python} copy_preserve_multi.py ${in} ${out}"
    restat = true
}
rule concat { command = "${python} concat.py ${out} ${in}" }
build "a.txt", "b.txt" : gen_mp "src.in"
build "final.txt" : concat "a.txt" "b.txt"
alias all : "final.txt"
''',
    "src.in": "multi_restat\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
    {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="restat + multi-output: mtime не изменился → downstream skip")


test("real_restat_with_depfile", {
    "build.pmake": '''pmake_required_version = 1.0
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
''',
    "main.c": "main\n",
    "common.h": "// v1\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
    {"name": "touch_h", "before": [{"sleep": 0.3}, {"touch": ["common.h"]}],
     "expect": {"exit": 0, "taskCount": 2}},
], description="restat + depfile (cc_make_dep всегда touch — restat не срабатывает, поведение документируем)")


# ============================================================
# 3. COMMAND HASH
# ============================================================
test("real_command_change", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} -O2" }
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "filesWith": {"out.txt": "-O2"}}},
    {"name": "change_command",
     "before": [{"sleep": 0.3},
                {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} -O3" }
build "out.txt" : echo
alias all : "out.txt"
''')}}],
     "expect": {"exit": 0, "taskCount": 1,
                "filesWith": {"out.txt": "-O3"}}},
], description="command hash: -O2 → -O3 → rebuild")


test("real_command_var_change", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
flags = "-O2"
rule echo { command = "${python} echo_to.py ${out} ${flags}" }
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
    {"name": "change_var", "before": [{"sleep": 0.3},
                                      {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
flags = "-O3"
rule echo { command = "${python} echo_to.py ${out} ${flags}" }
build "out.txt" : echo
alias all : "out.txt"
''')}}],
     "expect": {"exit": 0, "taskCount": 1,
                "filesWith": {"out.txt": "-O3"}}},
], description="command hash: flags -O2 → -O3")


test("real_command_hash_chain_3", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy   { command = "${python} copy.py ${in} ${out}" }
rule with_flag { command = "${python} echo_to.py ${out} FLAG_V1" }
build "a.out" : copy "src.in"
build "b.out" : copy "a.out"
build "c.out" : with_flag "b.out"
alias all : "c.out"
'''),
    "src.in": "chain\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                 "filesWith": {"c.out": "FLAG_V1"}}},
    {"name": "change_c_only",
     "before": [{"sleep": 0.3},
                {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy   { command = "${python} copy.py ${in} ${out}" }
rule with_flag { command = "${python} echo_to.py ${out} FLAG_V2" }
build "a.out" : copy "src.in"
build "b.out" : copy "a.out"
build "c.out" : with_flag "b.out"
alias all : "c.out"
''')}}],
     "expect": {"exit": 0, "taskCount": 1,
                "filesWith": {"c.out": "FLAG_V2"}}},
], description="command hash chain: change flag на c → только c rebuild")


# ============================================================
# 4. DEPFILE
# ============================================================
test("real_depfile_lost", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc {
    command = "${python} cc_make_dep.py ${in} ${out} common.h"
    depfile = "${out}.d"
    deps = "gcc"
}
build "main.o" : cc "main.c"
alias all : "main.o"
''',
    "main.c": "main\n",
    "common.h": "// v1\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
    {"name": "lost_d_touch_h",
     "before": [{"sleep": 0.3},
                {"delete": ["Project-cache.pmake", "main.o.d"]},
                {"touch": ["common.h"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="depfile lost → force rebuild")


test("real_depfile_persists", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc {
    command = "${python} cc_make_dep.py ${in} ${out} hdr.h"
    depfile = "${out}.d"
    deps = "gcc"
}
build "main.o" : cc "main.c"
alias all : "main.o"
''',
    "main.c": "main\n",
    "hdr.h": "// v1\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
    {"name": "touch_hdr", "before": [{"sleep": 0.3}, {"touch": ["hdr.h"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="depfile persists")


# ============================================================
# 5. ERROR RECOVERY
# ============================================================
test("real_error_recovery", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc {
    command = "${python} cc_maybe_fail.py ${in} ${out}"
    depfile = "${out}.d"
    deps = "gcc"
}
rule link { command = "${python} concat.py ${out} ${in}" }
build "a.o" : cc "a.c"
build "b.o" : cc "b.c"
build "app" : link "a.o" "b.o"
alias all : "app"
''',
    "a.c": "int a;\n",
    "b.c": "FAIL\n",
    "common.h": "// v1\n",
}, runs=[
    {"name": "first", "expect": {"exit": 1,
                                 "files": ["a.o"],
                                 "filesNot": ["b.o", "app"]}},
    {"name": "fix_and_retry",
     "before": [{"sleep": 0.3}, {"modify": {"b.c": "int b;\n"}}],
     "expect": {"exit": 0, "taskCount": 2,
                "files": ["b.o", "app"]}},
], description="fail → fix → только b.o + app")


test("real_partial_then_dry_run", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule ok   { command = "${python} echo_to.py ${out} OK" }
rule fail { command = "${python} fail.py" }
build "a.txt" : ok
build "b.txt" : fail "a.txt"
build "c.txt" : ok "b.txt"
alias all : "c.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 1, "files": ["a.txt"]}},
    {"name": "dry", "args": ["-f", "build.pmake", "-n"],
     "expect": {"exit": 0, "taskCount": 2}},
], description="fail: -n показывает b + c")


test("real_cache_saved_before_build", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule fail { command = "${python} fail.py" }
build "out.txt" : fail
alias all : "out.txt"
''',
}, runs=[
    {"name": "first_fail", "expect": {"exit": 1}},
    {"name": "cache_exists", "args": ["-f", "build.pmake", "-n"],
     "expect": {"exit": 0, "files": ["Project-cache.pmake"]}},
], description="fail: кэш до сборки")


# ============================================================
# 6. MULTI-OUTPUT, FANOUT
# ============================================================
test("real_multi_output", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} multi_out.py ${out}" }
build "a.txt", "b.txt", "c.txt" : gen
alias all : "a.txt", "b.txt", "c.txt"
''',
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                 "files": ["a.txt", "b.txt", "c.txt"]}},
    {"name": "noop", "before": [{"sleep": 0.3}],
     "expect": {"exit": 0, "taskCount": 0}},
    {"name": "delete_b", "before": [{"sleep": 0.3}, {"delete": ["b.txt"]}],
     "expect": {"exit": 0, "taskCount": 1}},
], description="multi-output: один таск генерит 3 файла — taskCount=1")


test("real_wide_fanout", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
''' + "\n".join(f'build "w_{i}.out" : copy "src.in"' for i in range(1, 21)) +
'\nalias all : ' + ", ".join(f'"w_{i}.out"' for i in range(1, 21))),
    "src.in": "fanout\n",
}, runs=[
    {"name": "first", "expect": {"exit": 0, "taskCount": 20}},
    {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
     "expect": {"exit": 0, "taskCount": 20}},
], description="1 → 20 fanout")


# ============================================================
# 7. PARALLEL
# ============================================================
test("real_parallel_correctness", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} echo_to.py ${out} MARKER_${marker}" }
''' + "\n".join(f'build "p_{i}.txt" : gen {{ marker = "id_{i}" }}'
               for i in range(1, 16)) +
'\nalias all : ' + ", ".join(f'"p_{i}.txt"' for i in range(1, 16))),
}, runs=[
    {"name": "j8", "args": ["-f", "build.pmake", "-j", "8"],
     "expect": {"exit": 0, "taskCount": 15,
                "filesWith": {f"p_{i}.txt": f"MARKER_id_{i}"
                              for i in range(1, 16)}}},
], description="15 задач -j 8")


# ИСПРАВЛЕНО: taskCountMin/Max вместо точного 1 —
# при -j8 в полёте могут оказаться p_*.txt, которые pmake не успеет запустить.
test("real_parallel_partial_fail", {
    "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule ok   { command = "${python} echo_to.py ${out} OK" }
rule fail { command = "${python} fail.py" }
''' + "\n".join(f'build "p_{i}.txt" : ok' for i in range(1, 6)) +
'\nbuild "bad.txt" : fail' +
'\nbuild "p_6.txt" : ok' +
'\nalias all : ' + ", ".join(f'"p_{i}.txt"' for i in range(1, 7)) + ', "bad.txt"'),
}, runs=[
    {"name": "j8_fail", "args": ["-f", "build.pmake", "-j", "8"],
     "expect": {"exit": 1}},
    {"name": "dry_after", "args": ["-f", "build.pmake", "-n"],
     "expect": {"exit": 0, "taskCountMin": 1, "taskCountMax": 7}},
], description="-j 8 fail: dry показывает 1..7 незавершённых")


test("real_parallel_diamond_j8", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy   { command = "${python} copy.py ${in} ${out}" }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "b.out" : copy "a.in"
build "c.out" : copy "a.in"
build "d.out" : copy "b.out"
build "e.out" : copy "c.out"
build "f.out" : concat "d.out" "e.out"
alias all : "f.out"
''',
    "a.in": "diamond8\n",
}, runs=[
    {"name": "j8", "args": ["-f", "build.pmake", "-j", "8"],
     "expect": {"exit": 0, "taskCount": 5,
                "files": ["b.out", "c.out", "d.out", "e.out", "f.out"]}},
], description="Diamond -j 8")


# ============================================================
# 8. SANITY
# ============================================================
test("real_sanity_unknown_target", {
    "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
''',
}, runs=[
    {"name": "unknown",
     "args": ["-f", "build.pmake", "nonexistent_target"],
     "expect": {"exit": 1}},
], description="pmake nonexistent_target → exit 1")


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
    # Считаем УНИКАЛЬНЫЕ пары (N, M) из "[N/M]" —
    # multi-output не завышает счётчик, если pmake печатает "[1/1]" один раз на таск.
    # Если pmake печатает "[1/1] a.txt", "[1/1] b.txt", "[1/1] c.txt" —
    # все три строки схлопнутся в одну пару (1, 1) → taskCount=1.
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
        sys.exit(1)

    helpers_dir.mkdir(parents=True, exist_ok=True)
    results_dir.mkdir(parents=True, exist_ok=True)

    for d in tests_dir.iterdir():
        if d.is_dir() and d.name.startswith("extra_"):
            shutil.rmtree(d)

    for name, content in HELPERS.items():
        (helpers_dir / name).write_text(content.lstrip(), encoding="utf-8")

    for t in TESTS_REAL:
        td = tests_dir / t["name"]
        if td.exists(): shutil.rmtree(td)
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

    existing = len([d for d in tests_dir.iterdir()
                    if d.is_dir() and (d / "meta.json").exists()])

    print()
    print("=" * 72)
    print(f" [OK] Real tests: {len(TESTS_REAL)}")
    print(f"      Total:      {existing}")
    print("=" * 72)
    print()
    print(" ИСПРАВЛЕНО:")
    print("   copy_preserve.py: MISMATCH больше НЕ восстанавливает mtime")
    print("                     (restat сможет пропустить downstream при реальном изменении контента)")
    print("   real_parallel_partial_fail: taskCountMin/Max вместо точного 1")
    print("   taskCount: считаются уникальные пары (N, M), multi-output не завышает")
    print("   + real_restat_multi_output")
    print("   + real_sanity_unknown_target")
    print()
    print(" Запуск:")
    print(f'   {REAL_PYTHON} "{root / "run.py"}" --pmake pmake.exe --filter "real_*"')
    print("=" * 72)


if __name__ == "__main__":
    main()