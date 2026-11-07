# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("depfile_basic", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule mk { command = "${python} make_dep.py ${out} hdr.h" depfile = "${out}.d" deps = "gcc" }
build "out.o" : mk "hdr.h"
alias all : "out.o"
''',
        "hdr.h": "/* v1 */\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.o"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_header", "before": [{"sleep": 0.5}, {"touch": ["hdr.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile: touch заголовка → пересборка (однострочный rule)"),

    test("depfile_unrelated", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule mk { command = "${python} make_dep.py ${out} hdr.h" depfile = "${out}.d" deps = "gcc" }
build "out.o" : mk "hdr.h"
alias all : "out.o"
''',
        "hdr.h": "/* v1 */\n", "other.h": "/* unrelated */\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "touch_unrelated",
         "before": [{"sleep": 0.5}, {"touch": ["other.h"]}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="depfile: touch нерелевантного → НЕ пересборка (однострочный rule)"),

    test("real_depfile_lost", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_make_dep.py ${in} ${out} common.h" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
''',
        "main.c": "main\n", "common.h": "// v1\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "lost_d_touch_h",
         "before": [{"sleep": 0.3}, {"delete": ["Project-cache.pmake", "main.o.d"]},
                    {"touch": ["common.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile lost → force rebuild (однострочный rule)"),

    test("real_depfile_persists", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_make_dep.py ${in} ${out} hdr.h" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
''',
        "main.c": "main\n", "hdr.h": "// v1\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "touch_hdr", "before": [{"sleep": 0.3}, {"touch": ["hdr.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile persists (однострочный rule)"),

    # NEW: bug #3 — `:` в Windows drive-letter путях не должен ломать парсер.
    # Старый парсер на "C:\build\main.o: main.c C:\src\hdr.h" выдавал:
    #   "\build\main.o main.c C \src\hdr.h"
    # Новый — "main.c C:\src\hdr.h". Проверяем через builtin dep_inputs().
    test("real_dep_parser_drive_letter", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
inputs = dep_inputs("test.d")
rule write { command = "${python} write_all.py ${out} ${inputs}" }
build "out.txt" : write
alias all : "out.txt"
''',
        "test.d": "C:\\build\\main.o: main.c C:\\src\\hdr.h\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "main.c C:\\src\\hdr.h"}}},
    ], description="dep_parser: drive-letter пути в target и inputs"),

    # NEW: дополнительно — drive letter в target, обычные inputs.
    test("real_dep_parser_drive_letter_target", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
inputs = dep_inputs("test.d")
rule write { command = "${python} write_all.py ${out} ${inputs}" }
build "out.txt" : write
alias all : "out.txt"
''',
        "test.d": "C:\\out\\main.o: a.h b.h\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "a.h b.h"}}},
    ], description="dep_parser: drive-letter в target, обычные inputs"),
]