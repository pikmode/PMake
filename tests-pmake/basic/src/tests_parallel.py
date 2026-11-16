# -*- coding: utf-8 -*-
from ._common import test, pp

TESTS = [
    test("parallel_race", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule noop { command = "${python} touch.py ${out}" }
''' + "\n".join(f'build "r_{i}.txt" : noop' for i in range(1, 31)) +
'\nalias all : ' + ", ".join(f'"r_{i}.txt"' for i in range(1, 31)) + "\n"),
    }, runs=[
        {"name": "parallel_30", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 30,
                    "files": [f"r_{i}.txt" for i in range(1, 31)]}},
    ], description="30 задач параллельно -j 8"),

    test("parallel_speed", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule slow { command = "${python} sleep_and_touch.py 200 ${out}" }
''' + "\n".join(f'build "s_{i}.txt" : slow' for i in range(1, 9)) +
'\nalias all : ' + ", ".join(f'"s_{i}.txt"' for i in range(1, 9)) + "\n"),
    }, runs=[
        {"name": "parallel_fast", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 8,
                    "files": [f"s_{i}.txt" for i in range(1, 9)]}},
    ], description="8 задач × 200ms sleep"),

    test("real_parallel_correctness", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} echo_to.py ${out} MARKER_${marker}" }
''' + "\n".join(f'build "p_{i}.txt" : gen {{ marker = "id_{i}" }}' for i in range(1, 16)) +
'\nalias all : ' + ", ".join(f'"p_{i}.txt"' for i in range(1, 16))),
    }, runs=[
        {"name": "j8", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 15,
                    "filesWith": {f"p_{i}.txt": f"MARKER_id_{i}" for i in range(1, 16)}}},
    ], description="15 задач -j 8"),

    test("real_parallel_partial_fail", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule ok { command = "${python} echo_to.py ${out} OK" }
rule fail { command = "${python} fail.py" }
''' + "\n".join(f'build "p_{i}.txt" : ok' for i in range(1, 6)) +
'\nbuild "bad.txt" : fail' + '\nbuild "p_6.txt" : ok' +
'\nalias all : ' + ", ".join(f'"p_{i}.txt"' for i in range(1, 7)) + ', "bad.txt"'),
    }, runs=[
        {"name": "j8_fail", "args": ["-f", "build.pmake", "-j", "8"], "expect": {"exit": 1}},
        {"name": "dry_after", "args": ["-f", "build.pmake", "-n"],
         "expect": {"exit": 0, "taskCountMin": 1, "taskCountMax": 7}},
    ], description="-j 8 fail"),

    test("real_parallel_diamond_j8", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
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
    ], description="Diamond -j 8"),
]
