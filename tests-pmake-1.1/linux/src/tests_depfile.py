# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_depfile_linux_paths", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
inputs = dep_inputs("test.d")
rule write { command = "${python} write_all.py ${out} ${inputs}" }
build "out.txt" : write
alias all : "out.txt"
''',
        # Linux формат .d файла с абсолютными путями
        "test.d": "/tmp/build/main.o: /usr/include/stdio.h /usr/include/stdlib.h\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "/usr/include/stdio.h /usr/include/stdlib.h"}}},
    ], description="dep_parser: абсолютные пути Linux в .d"),

    test("real_depfile_lost_linux", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_make_dep.py ${in} ${out} /usr/include/stdio.h" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
''',
        "main.c": "main\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "lost_d_touch_h",
         "before": [{"sleep": 0.3}, {"delete": ["Project-cache.pmake", "main.o.d"]},
                    {"touch": ["/usr/include/stdio.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile lost → force rebuild (Linux)"),
]
