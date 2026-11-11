# -*- coding: utf-8 -*-
from ._common import test, pp


def _rsp(n=100):
    files = {f"in_{i}.txt": f"content_{i}\n" for i in range(1, n + 1)}
    inputs = " ".join(f'"in_{i}.txt"' for i in range(1, n + 1))
    files["build.pmake"] = (
        'pmake_required_version = 1.0\n'
        'python = "@PYTHON@"\n'
        'rule link_all {\n'
        '    command = "${python} cat_rspfile.py ${rspfile} ${out}"\n'
        '    rspfile = "${out}.rsp"\n'
        '    rspfile_content = "${in}"\n'
        '}\n'
        f'build "out.txt" : link_all {inputs}\n'
        'alias all : "out.txt"\n'
    )
    expected = " ".join(f"in_{i}.txt" for i in range(1, n + 1))
    return files, n, expected


_rsp_files, _rsp_n, _rsp_expected = _rsp(100)


def _stress(n=200):
    lines = [
        'pmake_required_version = 1.0',
        'python = "@PYTHON@"',
        'rule touch {\n'
        '    command = "${python} touch.py ${out}"\n'
        '}',
    ]
    for i in range(1, n + 1):
        lines.append(f'build "s_{i:03d}.txt" : touch')
    lines.append('alias all : ' + ", ".join(f'"s_{i:03d}.txt"' for i in range(1, n + 1)))
    return {"build.pmake": "\n".join(lines) + "\n"}, n


_stress_files, _stress_n = _stress(200)

TESTS = [
    test("real_cache_after_failure", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc {
    command = "${python} cc_maybe_fail.py ${in} ${out}"
}
rule link {
    command = "${python} concat.py ${out} ${in}"
}
''' + "\n".join(f'build "f_{i:02d}.o" : cc "f_{i:02d}.c"' for i in range(1, 31)) +
'\nbuild "app" : link ' + " ".join(f'"f_{i:02d}.o"' for i in range(1, 31)) +
'\nalias all : "app"'),
        **{f"f_{i:02d}.c": ("FAIL\n" if i == 15 else f"int v_{i};\n") for i in range(1, 31)},
    }, runs=[
        {"name": "first_fail", "args": ["-f", "build.pmake", "-j", "1"],
         "expect": {"exit": 1,
                    "filesNot": ["f_15.o", "app"]}},
        {"name": "fix_and_retry",
         "before": [{"sleep": 0.3}, {"modify": {"f_15.c": "int v_15;\n"}}],
         "args": ["-f", "build.pmake", "-j", "1"],
         "expect": {"exit": 0,
                    "files": [f"f_{i:02d}.o" for i in range(1, 31)] + ["app"]}},
    ], description="30 files, f_15 fails → fix", timeout=120),

    test("real_rspfile_large", _rsp_files, runs=[
        {"name": "build_large_rsp",
         "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"],
                    "filesWith": {"out.txt": _rsp_expected}}},
    ], description=f"rspfile with {_rsp_n} inputs"),

    test("real_parallel_stress", _stress_files, runs=[
        {"name": "j16_stress", "args": ["-f", "build.pmake", "-j", "16"],
         "expect": {"exit": 0, "taskCount": 200,
                    "files": [f"s_{i:03d}.txt" for i in range(1, 201)]}},
    ], description="200 tasks -j16 stress", timeout=180),

    test("real_truncated_cache", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy {
    command = "${python} copy.py ${in} ${out}"
}
rule concat {
    command = "${python} concat.py ${out} ${in}"
}
build "a.out" : copy "a.in"
build "b.out" : copy "b.in"
build "app" : concat "a.out" "b.out"
alias all : "app"
''',
        "a.in": "A\n", "b.in": "B\n",
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
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule slow {
    command = "${python} delayed_echo.py 300 ${out} content"
}
''' + "\n".join(f'build "k_{i}.txt" : slow' for i in range(1, 11)) +
'\nalias all : ' + ", ".join(f'"k_{i}.txt"' for i in range(1, 11))),
    }, runs=[
        {"name": "kill_during", "args": ["-f", "build.pmake", "-j", "2"],
         "kill_after_ms": 700, "continue": True, "expect": {}},
        {"name": "resume_after_kill", "before": [{"sleep": 0.5}],
         "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "files": [f"k_{i}.txt" for i in range(1, 11)]}},
    ], description="Ctrl+C mid-build → restart completes", timeout=90),
]