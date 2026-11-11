# -*- coding: utf-8 -*-
from ._common import test, pp, SHELL_PREFIX

TESTS = [
    test("command_shell", {
        "build.pmake": pp(f'''pmake_required_version = 1.0
rule echo_chain {{
    command = "{SHELL_PREFIX} \\"echo A > a.txt && echo B > b.txt && echo C > out.txt\\""
}}
build "out.txt" : echo_chain
alias all : "out.txt"
'''),
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "files": ["a.txt", "b.txt", "out.txt"],
                                     "filesWith": {"a.txt": "A", "b.txt": "B", "out.txt": "C"}}},
    ], description="shell && через {SHELL_PREFIX}"),

    test("dry_run", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} never" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "dry", "args": ["-f", "build.pmake", "-n"],
         "expect": {"exit": 0, "taskCount": 1, "filesNot": ["out.txt"]}},
    ], description="-n не создаёт файлы"),

    test("clean", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "build_first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
        {"name": "clean_cmd", "args": ["clean", "-f", "build.pmake"],
         "expect": {"exit": 0, "filesNot": ["out.txt"]}},
    ], description="pmake clean удаляет outputs"),

    test("cache_delete", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out.c" : copy "src.c"
alias all : "out.c"
''',
        "src.c": "int v = 1;\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.c"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_cache_noop",
         "before": [{"sleep": 0.5}, {"delete": ["Project-cache.pmake"]}],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_cache_and_modify",
         "before": [{"sleep": 0.5}, {"delete": ["Project-cache.pmake"]},
                    {"modify": {"src.c": "int v = 2;\n"}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.c": "int v = 2;"}}},
    ], description="Кэш — ускоритель: удаление не форсит rebuild"),

    test("real_partial_then_dry_run", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule ok { command = "${python} echo_to.py ${out} OK" }
rule fail { command = "${python} fail.py" }
build "a.txt" : ok
build "b.txt" : fail "a.txt"
build "c.txt" : ok "b.txt"
alias all : "c.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 1, "files": ["a.txt"]}},
        {"name": "dry", "args": ["-f", "build.pmake", "-n"],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="fail: -n показывает b + c"),

    test("real_cache_saved_before_build", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule fail { command = "${python} fail.py" }
build "out.txt" : fail
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first_fail", "expect": {"exit": 1}},
        {"name": "cache_exists", "args": ["-f", "build.pmake", "-n"],
         "expect": {"exit": 0, "files": ["Project-cache.pmake"]}},
    ], description="fail: кэш до сборки"),

    test("real_multi_output", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen { command = "${python} multi_out.py ${out}" }
build "a.txt", "b.txt", "c.txt" : gen
alias all : "a.txt", "b.txt", "c.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["a.txt", "b.txt", "c.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_b", "before": [{"sleep": 0.3}, {"delete": ["b.txt"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="multi-output"),

    # NEW (баг #8): outputs при expand переменной должны разбиваться
    # по пробелам, как inputs. Сейчас "${outs}" резолвится в одну строку
    # "a.txt b.txt" — один output с пробелом в имени. Alias "a.txt", "b.txt"
    # их не находит (это отдельные leaf-ы), сборка не запускается.
    test("real_outputs_var_split", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
outs = "a.txt b.txt"
rule mk { command = "${python} touch.py ${out}" }
build "${outs}" : mk
alias all : "a.txt", "b.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "files": ["a.txt", "b.txt"]}},
    ], description="outputs split по пробелам при expand"),

    test("real_wide_fanout", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
''' + "\n".join(f'build "w_{i}.out" : copy "src.in"' for i in range(1, 21)) +
'\nalias all : ' + ", ".join(f'"w_{i}.out"' for i in range(1, 21))),
        "src.in": "fanout\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 20}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 20}},
    ], description="1 → 20 fanout"),

    test("real_new_target", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
alias all : "a.out"
'''),
        "a.in": "new_target_a\n", "b.in": "new_target_b\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["a.out"]}},
        {"name": "add_b_target",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
build "b.out" : copy "b.in"
alias all : "a.out", "b.out"
''')}}],
         "expect": {"exit": 0, "taskCount": 1, "files": ["b.out"]}},
    ], description="добавили build → только новый пересобирается"),

    test("real_build_file_comment", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "add_comment",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
// just a comment
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
''')}}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="комментарий не форсит rebuild"),

    test("real_sanity_unknown_target", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "unknown", "args": ["-f", "build.pmake", "nonexistent_target"],
         "expect": {"exit": 1}},
    ], description="pmake nonexistent_target → exit 1"),

    test("real_version", {
        "build.pmake": "pmake_required_version = 1.0\nalias all :\n",
    }, runs=[
        {"name": "version", "args": ["-V"],
         "expect": {"exit": 0, "stdoutMatches": [r"\d+\.\d+"]}},
    ], description="pmake -V"),

    test("real_help", {
        "build.pmake": "pmake_required_version = 1.0\nalias all :\n",
    }, runs=[
        {"name": "help", "args": ["--help"],
         "expect": {"exit": 0, "stdoutMatches": ["(?i)usage|option"]}},
    ], description="pmake --help"),

    test("real_no_build_file", {}, runs=[
        {"name": "no_file", "args": ["-f", "build.pmake"],
         "expect": {"exit": 1, "stderrMatches": ["(?i)cannot read|no build file|build\\.pmake"]}},
    ], description="нет build.pmake"),
]