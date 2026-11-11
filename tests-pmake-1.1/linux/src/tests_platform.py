# -*- coding: utf-8 -*-
from ._common import test, IS_WINDOWS

# Ожидаемое значение в зависимости от ОС
EXPECTED_FLAG = "IS_WINDOWS" if IS_WINDOWS else "IS_LINUX"

TESTS = [
    test("real_platform_windows_applied", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
platform windows {
    os_flag = "IS_WINDOWS"
}
platform linux {
    os_flag = "IS_LINUX"
}
rule echo { command = "${python} write_all.py ${out} ${os_flag}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": EXPECTED_FLAG}}},
    ], description="OS platform block applied automatically")
]