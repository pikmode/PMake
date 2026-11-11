# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
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
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="Минимум: одно правило, один build, alias"),

    test("two_builds", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "a.txt" : echo
build "b.txt" : echo
alias all : "a.txt", "b.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["a.txt", "b.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="Два независимых build"),

    test("alias_multi", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} y" }
build "x.txt" : echo
build "y.txt" : echo
build "z.txt" : echo
alias all : "x.txt", "y.txt", "z.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                     "files": ["x.txt", "y.txt", "z.txt"]}},
    ], description="alias с 3 targets"),
]
