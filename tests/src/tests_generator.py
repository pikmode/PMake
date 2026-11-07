# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
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
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["gen.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_output", "before": [{"sleep": 0.3}, {"delete": ["gen.txt"]}],
         "expect": {"exit": 0, "taskCount": 1, "files": ["gen.txt"]}},
    ], description="generator: exists → skip; missing → rebuild"),

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
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["gen.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_src_should_rebuild",
         "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="generator mtime"),
]