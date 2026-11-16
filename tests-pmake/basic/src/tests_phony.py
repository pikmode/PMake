# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_phony_order_basic", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule slow_touch { command = "${python} sleep_and_touch.py 400 ${out}" }
rule check_stamp { command = "${python} check_stamp.py stamp.txt ${in} ${out}" }
build "stamp.txt" : slow_touch
build "phony_stamp" : phony "stamp.txt"
build "out.txt" : check_stamp "out.in" || "phony_stamp"
alias all : "out.txt"
''',
        "out.in": "out\n",
    }, runs=[
        {"name": "j2_ordered", "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="phony (input): порядок через BFS"),

    test("real_phony_order_transitive", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule slow_touch { command = "${python} sleep_and_touch.py 300 ${out}" }
rule check_two { command = "${python} check_two.py step1.txt step2.txt ${out}" }
build "step1.txt" : slow_touch
build "step2.txt" : slow_touch
build "phony1" : phony "step1.txt"
build "phony2" : phony "step2.txt" "phony1"
build "out.txt" : check_two || "phony2"
alias all : "out.txt"
''',
    }, runs=[
        {"name": "j2_ordered", "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "taskCount": 3, "files": ["step1.txt", "step2.txt", "out.txt"]}},
    ], description="phony chain (input)"),

    test("real_phony_order_cmake", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule slow_touch { command = "${python} sleep_and_touch.py 400 ${out}" }
rule check_stamp { command = "${python} check_stamp.py autogen_stamp ${in} ${out}" }
build "autogen_stamp" : slow_touch
build "cmake_obj_deps" : phony || "autogen_stamp"
build "out.txt" : check_stamp "out.in" || "cmake_obj_deps"
alias all : "out.txt"
''',
        "out.in": "out\n",
    }, runs=[
        {"name": "j2_ordered", "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "taskCount": 2, "files": ["autogen_stamp", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="CMake-паттерн phony ||"),

    test("real_phony_order_noop", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "stamp.txt" : copy "stamp.in"
build "phony_stamp" : phony "stamp.txt"
build "out.txt" : copy "out.in" || "phony_stamp"
alias all : "out.txt"
''',
        "stamp.in": "stamp\n", "out.in": "out\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_stamp", "before": [{"sleep": 0.3}, {"touch": ["stamp.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="phony + order-only: noop не ломается"),
]
