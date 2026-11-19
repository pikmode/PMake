# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("ninja_clean_and_help_targets", {
        "print_args.py": "import sys\nprint(' '.join(sys.argv[1:]))\n",
        "build.ninja": '''rule CLEAN
  command = @PYTHON@ print_args.py clean tool ran
  description = Cleaning all built files...

rule HELP
  command = @PYTHON@ print_args.py help tool ran
  description = All primary targets available:

rule NORMAL
  command = @PYTHON@ print_args.py normal rule ran

build clean: CLEAN
build help: HELP
build normal: NORMAL
''',
    }, runs=[
        {"name": "clean_target", "args": ["-f", "build.ninja", "clean"],
         "expect": {"exit": 0, "stdoutHas": ["Cleaning all built files...", "clean tool ran"],
                    "filesNot": ["clean"]}},
        {"name": "help_target", "args": ["-f", "build.ninja", "help"],
         "expect": {"exit": 0, "stdoutHas": ["All primary targets available:", "help tool ran"],
                    "filesNot": ["help"]}},
        {"name": "normal_rule_requires_output", "args": ["-f", "build.ninja", "normal"],
         "expect": {"exit": 1, "stderrHas": ["Output file not created: normal"]}},
    ], description="Ninja CLEAN and HELP targets run successfully without output files"),

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