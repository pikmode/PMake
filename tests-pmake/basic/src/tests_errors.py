# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("error_cycle", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a" : copy "b"
build "b" : copy "a"
alias all : "a"
''',
    }, runs=[
        {"name": "cycle_should_fail", "expect": {"exit": 1, "stderrHas": ["ycl"]}},
    ], description="цикл в графе → ошибка"),

    test("error_missing_rule", {
        "build.pmake": 'pmake_required_version = 1.0\nbuild "out.txt" : nonexistent_rule\nalias all : "out.txt"\n',
    }, runs=[
        {"name": "missing_rule_should_fail", "expect": {"exit": 1}},
    ], description="несуществующее правило → ошибка"),

    test("error_bad_syntax", {
        "build.pmake": 'pmake_required_version = 1.0\nbuild "out.txt" :\nalias all : "out.txt"\n',
    }, runs=[
        {"name": "bad_syntax_should_fail", "expect": {"exit": 1}},
    ], description="синтаксическая ошибка → exit 1"),

    test("real_error_recovery", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_maybe_fail.py ${in} ${out}" }
rule link { command = "${python} concat.py ${out} ${in}" }
build "a.o" : cc "a.c"
build "b.o" : cc "b.c"
build "app" : link "a.o" "b.o"
alias all : "app"
''',
        "a.c": "int a;\n", "b.c": "FAIL\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 1, "files": ["a.o"], "filesNot": ["b.o", "app"]}},
        {"name": "fix_and_retry",
         "before": [{"sleep": 0.3}, {"modify": {"b.c": "int b;\n"}}],
         "expect": {"exit": 0, "taskCount": 2, "files": ["b.o", "app"]}},
    ], description="fail → fix → только b.o + app"),

    # NEW (баг #7): если input удалён, pmake сейчас говорит
    # "All targets up to date" (mtime -1 не считается > output),
    # и молча продолжает с устаревшим output. Должен быть exit 1:
    # либо "missing input" pre-check, либо форсированный rebuild
    # с провалом на чтении input.
    test("real_missing_input", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out.txt" : copy "in.txt"
alias all : "out.txt"
''',
        "in.txt": "content\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
        {"name": "delete_input",
         "before": [{"sleep": 0.3}, {"delete": ["in.txt"]}],
         "expect": {"exit": 1}},
    ], description="удалённый input → ошибка, а не «up to date»"),

    # ============================================================
    # NEW: регрессии для сообщения про '#'
    #
    # После патча lexer.cpp (case '#' в readSymbol) и удаления
    # print из Lexer::error + ParserImpl::error/advance:
    #   * при '# пользователь видит подсказку про // и /* */
    #   * ошибка печатается РОВНО ОДИН РАЗ (не 3 копии)
    #   * '#' внутри строки остаётся частью строки (не комментарий)
    # ============================================================
    test("error_hash_comment_hint", {
        "build.pmake": '''pmake_required_version = 1.0
# this is a comment, but wrong style
alias all :
''',
    }, runs=[
        {"name": "hash_comment_should_fail",
         "expect": {
             "exit": 1,
             "stderrHas": [
                 "'#' is not a comment in PMake",
                 "Use '//' for line comments",
                 "/* */",
             ],
         }},
    ], description="# в pmake → подсказка про // комментарии"),

    test("error_hash_not_duplicated", {
        "build.pmake": 'pmake_required_version = 1.0\n# bad comment\n',
    }, runs=[
        {"name": "single_error_message",
         "expect": {
             "exit": 1,
             # Раньше было 3 копии (Lexer::error + advance + parse).
             # После патча — ровно одна.
             "stderrHasExactlyOnce": ["'#' is not a comment in PMake"],
         }},
    ], description="# не должен печататься трижды"),

    # sanity: '#' внутри строки — не комментарий, а символ.
    # Патч case '#' в readSymbol не должен задеть readString.
    test("hash_inside_string_ok", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} write_all.py ${out} #tag_value" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "hash_in_string",
         "expect": {"exit": 0,
                    "filesWith": {"out.txt": "#tag_value"}}},
    ], description="# внутри строки — часть строки, не ошибка"),
]