# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("builtin_glob", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
files = glob("*.src", ".")
rule echo { command = "${python} echo_to.py ${out} built" }
build "out.txt" : echo
alias all : "out.txt"
''',
        "a.src": "x", "b.src": "y",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
    ], description="glob() находит файлы"),

    test("append_flags", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
flags = "A"
flags += "B"
flags += "C"
rule echo { command = "${python} write_all.py ${out} ${flags}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "A B C"}}},
    ], description="+= конкатенация через пробел"),
]
