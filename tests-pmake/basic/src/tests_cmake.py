# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_cmake_full_pattern", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule autogen {
    command = "${python} sleep_and_touch.py 200 ${out}"
    restat = true
}
rule compile {
    command = "${python} check_stamp.py autogen_stamp ${in} ${out}"
}
build "autogen_stamp" : autogen "gen.in"
build "cmake_obj_deps" : phony || "autogen_stamp"
build "main.o" : compile "main.c" || "cmake_obj_deps"
build "app" : compile "main.o" || "cmake_obj_deps"
alias all : "app"
''',
        "gen.in": "gen\n", "main.c": "main\n",
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