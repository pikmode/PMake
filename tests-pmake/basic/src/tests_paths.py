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
    ], description="пробелы в именах файлов"),

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
    ], description="Unicode в именах файлов"),

    # NEW: проверяет fix bug #2 (lexer: `\t`, `\p`, `\b`, `\l`, `\s` в путях).
    # Старый лексер превращал `\t` в TAB, `\p` в `p`, `\s` в `s` и т.д.
    # Новый оставляет их как есть (кроме `\n`, `\r`, `\\`, `\"`).
    test("real_paths_backslash", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
p1 = "C:\\test\\path"
p2 = "C:\\temp\\build"
p3 = "C:\\src\\lib"
rule echo { command = "${python} write_all.py ${out} ${p1} ${p2} ${p3}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt":
                                         "C:\\test\\path C:\\temp\\build C:\\src\\lib"}}},
    ], description="backslash в путях сохраняется (\\t, \\p, \\b, \\l, \\s)"),
]