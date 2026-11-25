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
         "expect": {"exit": 0, "taskCount": 1, "filesNot": ["out.txt"],
                    "stdoutNot": ["Build succeeded"]}},
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
        {"name": "clean_positional_looks_up_manifest_target", "args": ["clean"],
         "expect": {"exit": 1, "stderrMatches": ["(?i)target.*clean.*not found"],
                    "files": ["out.txt"]}},
        {"name": "old_clean_flag_is_removed", "args": ["--clean"],
         "expect": {"exit": 1, "files": ["out.txt"]}},
        {"name": "clean_short", "args": ["-t", "clean", "-f", "build.pmake"],
         "expect": {"exit": 0, "filesNot": ["out.txt"]}},
        {"name": "build_again", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
        {"name": "clean_long", "args": ["--tools", "clean", "-f", "build.pmake"],
         "expect": {"exit": 0, "filesNot": ["out.txt"]}},
    ], description="utility clean removes outputs"),

    test("command_output_noop_and_target_clean", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule emit {
    command = "${python} emit_output.py ${out} ${in}"
}
build "dep.txt" : emit "src.txt"
build "app.txt" : emit "dep.txt"
build "extra.txt" : emit "src.txt"
alias all : "app.txt", "extra.txt"
''',
        "src.txt": "source\n",
        "emit_output.py": '''import pathlib
import sys

output = pathlib.Path(sys.argv[1])
inputs = [pathlib.Path(name) for name in sys.argv[2:]]
output.write_text(
    "".join(path.read_text(encoding="utf-8") for path in inputs),
    encoding="utf-8",
)
print(f"command stdout: {output}")
print(f"command stderr: {output}", file=sys.stderr)
''',
    }, runs=[
        {"name": "build_forwards_command_output",
         "expect": {"exit": 0, "taskCount": 3,
                    "stdoutHas": ["PMake version 1.3.2"],
                    "stdoutHasExactlyOnce": [
                        "command stdout: dep.txt",
                        "command stdout: app.txt",
                        "command stdout: extra.txt",
                    ],
                    "stderrHasExactlyOnce": [
                        "command stderr: dep.txt",
                        "command stderr: app.txt",
                        "command stderr: extra.txt",
                    ]}},
        {"name": "noop_has_no_logo_or_child_output",
         "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0,
                    "stdoutHas": ["All targets up to date"],
                    "stdoutNot": ["PMake version", "command stdout:"],
                    "stderrNot": ["command stderr:"]}},
        {"name": "clean_target_does_not_remove_its_inputs",
         "args": ["-f", "build.pmake", "-t", "clean", "app.txt"],
         "expect": {"exit": 0, "filesNot": ["app.txt"],
                    "files": ["dep.txt", "extra.txt", "src.txt"],
                    "stderrHas": ["Cleaned 1 files"]}},
        {"name": "commands_target_lists_only_its_dependency_chain",
         "args": ["-f", "build.pmake", "-t", "commands", "app.txt"],
         "expect": {"exit": 0,
                    "stdoutHas": ["emit_output.py dep.txt src.txt",
                                  "emit_output.py app.txt dep.txt"],
                    "stdoutNot": ["emit_output.py extra.txt"]}},
        {"name": "clean_unknown_target_fails_explicitly",
         "args": ["-f", "build.pmake", "-t", "clean", "missing.txt"],
         "expect": {"exit": 1, "stderrHas": ["clean: target 'missing.txt' not found"]}},
    ], description="commands always show child output; noop suppresses banner; targeted clean preserves inputs"),

    test("failed_command_forwards_both_output_streams", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule fail { command = "${python} fail_with_output.py ${out}" }
build "out.txt" : fail
alias all : "out.txt"
''',
        "fail_with_output.py": '''import pathlib
import sys

pathlib.Path(sys.argv[1]).write_text("partial", encoding="utf-8")
print("child stdout before failure")
print("child stderr before failure", file=sys.stderr)
sys.exit(7)
''',
    }, runs=[
        {"name": "failed_command_still_displays_its_output",
         "expect": {"exit": 1, "taskCount": 1,
                    "stdoutHasExactlyOnce": ["child stdout before failure"],
                    "stderrHasExactlyOnce": ["child stderr before failure"],
                    "filesNot": ["out.txt"]}},
    ], description="stdout and stderr from a failed build command are both forwarded before cleanup"),

    test("multiple_positional_targets", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} built" }
build "build" : echo
build "clean" : echo
build "help" : echo
''',
    }, runs=[
        {"name": "build_reserved_names_as_targets", "args": ["build", "clean", "help"],
         "expect": {"exit": 0, "taskCount": 3,
                    "files": ["build", "clean", "help"]}},
    ], description="all positional arguments are manifest targets"),

    test("tools_targets", {
        "build.pmake": '''pmake_required_version = 1.0
rule echo { command = "echo ${out}" }
build "dep.txt" : echo
build "app.txt" : echo "dep.txt"
alias all : "app.txt"
''',
    }, runs=[
        {"name": "list_targets", "args": ["-t", "targets"],
         "expect": {"exit": 0, "stdoutHas": ["dep.txt", "app.txt", "all"]}},
    ], description="targets tool lists build outputs and aliases"),

    test("tools_commands_target", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} ${value}" }
build "dep.txt" : echo { value = "dependency" }
build "app.txt" : echo "dep.txt" { value = "app" }
alias all : "app.txt"
''',
    }, runs=[
        {"name": "list_commands", "args": ["--tools", "commands", "app.txt"],
         "expect": {
             "exit": 0,
             "stdoutHas": ["echo_to.py dep.txt dependency", "echo_to.py app.txt app"],
             "filesNot": ["dep.txt", "app.txt"],
         }},
    ], description="commands tool expands target dependency commands without running them"),

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
        # CHANGED: раньше проверялся только exit 1. Но exit 1 мог быть
        # и от "unknown command 'nonexistent_target'" (до CLI-фикса).
        # Теперь — строго "target ... not found" от движка.
        {"name": "unknown", "args": ["-f", "build.pmake", "nonexistent_target"],
         "expect": {"exit": 1, "stderrMatches": ["(?i)target.*not found"]}},
    ], description="pmake nonexistent_target → exit 1 (target not found)"),

    test("real_version", {
        "build.pmake": "pmake_required_version = 1.0\nalias all :\n",
    }, runs=[
        {"name": "version", "args": ["-V"],
         "expect": {"exit": 0, "stdoutMatches": [r"PMake version 1\.3\.2"]}},
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

    # ============================================================
    # Регрессия: все позиционные аргументы являются именами целей.
    #
    # До фикса `main.cpp::parse_args` первый positional-аргумент
    # ВСЕГДА считался командой, а команда только одна — `build`.
    # Поэтому `pmake .generated`, `pmake all`, `pmake out.txt` — падали
    # с "unknown command '...'". `real_sanity_unknown_target` проходил
    # случайно (exit 1 и от этого тоже).
    #
    # Ни один текстовый аргумент не интерпретируется как команда.
    # ============================================================
    test("cli_target_positional", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} x" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "positional_target",
         "args": ["-f", "build.pmake", "out.txt"],
         "expect": {"exit": 0, "files": ["out.txt"],
                    "filesWith": {"out.txt": "x"}}},
        {"name": "alias_target",
         "args": ["-f", "build.pmake", "all"],
         "expect": {"exit": 0, "files": ["out.txt"]}},
    ], description="pmake <target> — make/ninja-style CLI"),
]