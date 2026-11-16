# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("foreach", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
names = "alpha beta gamma"
rule echo { command = "${python} echo_to.py ${out} ${n}" }
foreach n in ${names} {
    build "out_${n}.txt" : echo
}
alias all : "out_alpha.txt", "out_beta.txt", "out_gamma.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                     "files": ["out_alpha.txt", "out_beta.txt", "out_gamma.txt"]}},
    ], description="foreach: 3 build из списка"),

    test("if_true", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
mode = "release"
rule echo { command = "${python} echo_to.py ${out} ${tag}" }
if mode == "release" {
    tag = "R"
} else {
    tag = "D"
}
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "R"}}},
    ], description="if true → ветка then"),

    test("switch", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
mode = "debug"
switch mode {
case "debug" { rule echo { command = "${python} echo_to.py ${out} DEBUG" } }
case "release" { rule echo { command = "${python} echo_to.py ${out} RELEASE" } }
default { rule echo { command = "${python} echo_to.py ${out} UNKNOWN" } }
}
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "DEBUG"}}},
    ], description="switch/case/default"),

    test("function_call", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
function say(msg) { payload = "${msg}" }
say("hello_func")
rule echo { command = "${python} echo_to.py ${out} ${payload}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "hello_func"}}},
    ], description="user-function изменяет переменную"),

    # NEW (баг #5a): continue в foreach должен пропускать итерацию.
    # Сейчас BREAK/CONTINUE-узлы парсятся, но evaluateAst их игнорирует →
    # тело foreach выполняется для всех элементов.
    # Ожидаем: result = "a c d" (b пропущено).
    # Сейчас: result = "a b c d".
    test("real_foreach_continue", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
items = "a b c d"
result = ""
foreach x in ${items} {
    if x == "b" { continue }
    result += ${x}
}
rule write { command = "${python} write_all.py ${out} ${result}" }
build "out.txt" : write
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "a c d"}}},
    ], description="continue в foreach"),

    # NEW (баг #5b): break в foreach должен выходить из цикла.
    # Ожидаем: result = "a b" (остановка на c).
    # Сейчас: result = "a b c d e".
    test("real_foreach_break", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
items = "a b c d e"
result = ""
foreach x in ${items} {
    if x == "c" { break }
    result += ${x}
}
rule write { command = "${python} write_all.py ${out} ${result}" }
build "out.txt" : write
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "a b"}}},
    ], description="break в foreach"),
]