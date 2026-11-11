# -*- coding: utf-8 -*-
from ._common import test, pp

TESTS = [
    test("real_pool_console_serial", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule console_task {
    command = "${python} sleep_and_touch.py 350 ${out}"
    pool = console
}
''' + "\n".join(f'build "c_{i}.txt" : console_task' for i in range(1, 4)) +
'\nalias all : ' + ", ".join(f'"c_{i}.txt"' for i in range(1, 4))),
    }, runs=[
        {"name": "j8_serialized", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 3,
                    "files": ["c_1.txt", "c_2.txt", "c_3.txt"], "elapsedMin": 0.85}},
    ], description="pool=console сериализует при -j8"),

    test("real_pool_console_with_order", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule console_task {
    command = "${python} sleep_and_touch.py 300 ${out}"
    pool = console
}
build "stamp.txt" : console_task
build "out.txt" : console_task || "stamp.txt"
alias all : "out.txt"
''',
    }, runs=[
        {"name": "j8_serialized", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"],
                    "elapsedMin": 0.55}},
    ], description="pool=console + order-only"),
]