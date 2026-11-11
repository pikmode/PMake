# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("include_basic", {
        "build.pmake": '''pmake_required_version = 1.0
include "sub.pmake"
build "out.txt" : echo
alias all : "out.txt"
''',
        "sub.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} from_sub" }
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "from_sub"}}},
    ], description="include простого файла"),

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
    ], description="include с относительным путём"),

    test("include_cycle", {
        "build.pmake": 'pmake_required_version = 1.0\ninclude "a.pmake"\n',
        "a.pmake": 'pmake_required_version = 1.0\ninclude "b.pmake"\n',
        "b.pmake": 'pmake_required_version = 1.0\ninclude "a.pmake"\n',
    }, runs=[
        {"name": "cycle_detected", "expect": {"exit": 1, "stderrHas": ["ycl"]}},
    ], description="include cycle → ошибка, не крашить"),

    # NEW (баг #4a): sub.pmake использует переменную OUT_DIR, определённую
    # в родителе. Сейчас sub-парсер не видит родительских globals, поэтому
    # build-вывод "${OUT_DIR}/result.txt" резолвится в "/result.txt",
    # а alias в родителе таргетит "out_dir/result.txt" — разные узлы,
    # сборка не запускается. После фикса — один узел, build выполняется.
    test("real_include_parent_vars_in_build", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
OUT_DIR = "out_dir"
include "sub.pmake"
alias all : "${OUT_DIR}/result.txt"
''',
        "sub.pmake": '''pmake_required_version = 1.0
rule echo { command = "${python} echo_to.py ${out} hello" }
build "${OUT_DIR}/result.txt" : echo
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "files": ["out_dir/result.txt"],
                                     "filesWith": {"out_dir/result.txt": "hello"}}},
    ], description="include видит переменные родителя в build output"),

    # NEW (баг #4b): include внутри if false не должен выполняться.
    # Сейчас include исполняется на этапе парсинга (до evaluateIf),
    # поэтому pmake пытается открыть несуществующий файл и падает.
    # После фикса — include откладывается до eval, if false → не выполняется.
    test("real_include_skip_by_if_false", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
mode = "release"
if mode == "debug" {
    include "debug_only.pmake"
}
rule echo { command = "${python} echo_to.py ${out} ok" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "ok"}}},
    ], description="include внутри if false не выполняется"),
]