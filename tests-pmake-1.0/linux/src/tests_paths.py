# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_paths_spaces", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out file.txt" : copy "in file.txt"
alias all : "out file.txt"
''',
        "in file.txt": "spaces work\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out file.txt"],
                                     "filesWith": {"out file.txt": "spaces work"}}},
    ], description="пробелы в именах файлов (Linux)"),

    test("real_paths_unicode", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "файл.txt" : copy "исходник.txt"
alias all : "файл.txt"
''',
        "исходник.txt": "unicode ok\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["файл.txt"],
                                     "filesWith": {"файл.txt": "unicode ok"}}},
    ], description="Unicode в именах файлов (Linux)"),

    # Специфичный Linux-тест: абсолютные пути (/tmp)
    test("real_paths_linux_absolute", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} write_all.py ${out} ${msg}" }
build "/tmp/pmake_out_test.txt" : echo { msg = "absolute_path_ok" }
alias all : "/tmp/pmake_out_test.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
    ], description="абсолютные пути Linux (/tmp)"),
]
