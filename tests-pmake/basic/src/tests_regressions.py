# -*- coding: utf-8 -*-
from ._common import IS_WINDOWS, test, pp


def _rsp_manifest(content):
    return f'''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen {{
    command = "${{python}} cat_rspfile.py ${{rspfile}} ${{out}}"
    rspfile = "${{out}}.rsp"
    rspfile_content = "{content}"
}}
build "out.txt" : gen
alias all : "out.txt"
'''


def _multi_output_manifest(content):
    return f'''pmake_required_version = 1.0
python = "@PYTHON@"
rule emit {{ command = "${{python}} write_multi.py ${{out}} {content}" }}
build "a.txt", "b.txt" : emit
alias all : "a.txt", "b.txt"
'''


def _deep_alias_manifest(depth):
    lines = ["pmake_required_version = 1.0", "alias node0 :"]
    lines.extend(f'alias node{i} : "node{i - 1}"' for i in range(1, depth))
    return "\n".join(lines) + "\n"


TESTS = [
    test("regression_multi_output_secondary_target", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy_preserve_multi.py ${in} ${out}" }
build "a.txt", "b.txt" : copy "src.txt"
''',
        "src.txt": "first\n",
    }, runs=[
        {"name": "build_secondary_output",
         "args": ["-f", "build.pmake", "b.txt"],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {
             "a.txt": "first", "b.txt": "first"}}},
        {"name": "changed_input_rebuilds_secondary_output",
         "before": [{"sleep": 0.3}, {"modify": {"src.txt": "second\n"}}],
         "args": ["-f", "build.pmake", "b.txt"],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {
             "a.txt": "second", "b.txt": "second"}}},
    ], description="secondary output target must retain build dependencies"),

    test("regression_multi_output_secondary_command_hash", {
        "build.pmake": _multi_output_manifest("first"),
        "write_multi.py": '''import pathlib
import sys

for output in sys.argv[1:-1]:
    pathlib.Path(output).write_text(sys.argv[-1], encoding="utf-8")
''',
    }, runs=[
        {"name": "initial_primary_build",
         "expect": {"exit": 0, "taskCount": 1,
                    "filesWith": {"a.txt": "first", "b.txt": "first"}}},
        {"name": "secondary_output_detects_command_change",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake":
             pp(_multi_output_manifest("second"))}}],
         "args": ["-f", "build.pmake", "b.txt"],
         "expect": {"exit": 0, "taskCount": 1,
                    "filesWith": {"a.txt": "second", "b.txt": "second"}}},
    ], description="command fingerprints must be saved for every output of a build"),

    test("regression_included_manifest_change", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
include "rules.pmake"
build "out.txt" : emit
alias all : "out.txt"
''',
        "rules.pmake": '''rule emit { command = "${python} echo_to.py ${out} first" }
''',
    }, runs=[
        {"name": "initial_build",
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "first"}}},
        {"name": "included_rule_change_rebuilds",
         "before": [{"sleep": 0.3}, {"modify": {"rules.pmake":
             'rule emit { command = "${python} echo_to.py ${out} second" }\n'}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "second"}}},
    ], description="an included manifest change must invalidate the build cache"),

    test("regression_rspfile_content_change", {
        "build.pmake": _rsp_manifest("first"),
    }, runs=[
        {"name": "initial_build",
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "first"}}},
        {"name": "changed_rspfile_content_rebuilds",
         "before": [{"sleep": 0.3}, {"modify": {
             "build.pmake": pp(_rsp_manifest("second"))}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "second"}}},
    ], description="response-file content is part of the command fingerprint"),

    test("regression_partial_output_after_failure", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule compile { command = "${python} fail_once.py ${out}" }
build "out.txt" : compile "src.txt"
alias all : "out.txt"
''',
        "src.txt": "source\n",
        "fail_once.py": '''import pathlib
import sys

output = pathlib.Path(sys.argv[1])
marker = pathlib.Path(".failed-once")
if not marker.exists():
    output.write_text("partial", encoding="utf-8")
    marker.write_text("failed", encoding="utf-8")
    raise SystemExit(1)
output.write_text("complete", encoding="utf-8")
''',
    }, runs=[
        {"name": "first_attempt_leaves_partial_output",
         "expect": {"exit": 1, "taskCount": 1, "filesNot": ["out.txt"]}},
        {"name": "retry_must_replace_partial_output",
         "expect": {"exit": 0, "taskCount": 1,
                    "filesWith": {"out.txt": "complete"}}},
    ], description="a failed command's partial output must not be accepted as up to date"),

    test("regression_external_exit_two_is_failure", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule fail { command = "${python} exit_two.py" }
build "out.txt" : fail
alias all : "out.txt"
''',
        "exit_two.py": '''import sys
raise SystemExit(2)
''',
    }, runs=[
        {"name": "exit_two", "expect": {"exit": 1, "taskCount": 1}},
    ], description="an external process exit code 2 is not an internal restat result"),

    test("regression_generator_restat_noop_does_not_restart", {
        "build.ninja": '''rule regen
  command = @PYTHON@ regen.py $out
  generator = 1
  restat = 1
rule touch
  command = @PYTHON@ touch.py $out
build build.ninja: regen gen.in
build out.txt: touch
default out.txt
''',
        "next.ninja": '''rule touch
  command = @PYTHON@ touch.py $out
build out.txt: touch
default out.txt
''',
        "gen.in": "initial\n",
        "regen.py": '''import pathlib
import sys

marker = pathlib.Path(".regen-count")
if not marker.exists():
    marker.write_text("first", encoding="utf-8")
else:
    marker.write_text("second", encoding="utf-8")
    pathlib.Path(sys.argv[1]).write_text(
        pathlib.Path("next.ninja").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
''',
    }, runs=[
        {"name": "restat_noop",
         "before": [{"sleep": 0.3}, {"modify": {"gen.in": "changed\n"}}],
         "args": ["-f", "build.ninja"],
         "expect": {"exit": 0, "files": ["out.txt"],
                    "filesWith": {".regen-count": "first"}}},
    ], description="unchanged generator outputs should not trigger a restart"),

    test("regression_deep_graph_stack_safety", {
        "build.pmake": _deep_alias_manifest(12000),
    }, runs=[
        {"name": "deep_target",
         "args": ["-f", "build.pmake", "-n", "-q", "node11999"],
         "expect": {"exit": 0}},
    ], description="deep dependency graphs must not overflow the process stack",
       timeout=30),

    test("regression_clean_reports_removal_failure", {
        "build.pmake": '''pmake_required_version = 1.0
rule gen { command = "unused" }
build "nonempty" : gen
''',
        "nonempty/keep.txt": "keep\n",
    }, runs=[
        {"name": "clean_nonempty_directory",
         "args": ["-f", "build.pmake", "-t", "clean"],
         "expect": {"exit": 1, "files": ["nonempty/keep.txt"],
                    "stderrHas": ["nonempty"]}},
    ], description="clean must report failed output removal"),
]


if IS_WINDOWS:
    TESTS.extend([
        test("regression_windows_case_insensitive_stat", {
            "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "out.txt" : copy "source.txt"
alias all : "out.txt"
''',
            "SOURCE.txt": "case-insensitive input\n",
        }, runs=[
            {"name": "first_build", "expect": {"exit": 0, "taskCount": 1}},
            {"name": "case_mismatched_path_is_still_up_to_date",
             "before": [{"sleep": 0.3}],
             "expect": {"exit": 0, "taskCount": 0}},
        ], description="Windows file lookup must not treat path casing as identity"),

        test("regression_windows_restart_quotes_manifest_path", {
            "space build.ninja": '''rule regen
  command = @PYTHON@ regen.py $out
  generator = 1
rule touch
  command = @PYTHON@ touch.py $out
build space$ build.ninja: regen gen.in
build out.txt: touch
default out.txt
''',
            "next.ninja": '''rule touch
  command = @PYTHON@ touch.py $out
build out.txt: touch
default out.txt
''',
            "gen.in": "initial\n",
            "regen.py": '''import pathlib
import sys

pathlib.Path(sys.argv[1]).write_text(
    pathlib.Path("next.ninja").read_text(encoding="utf-8"),
    encoding="utf-8",
)
''',
        }, runs=[
            {"name": "regenerate_manifest_with_space_in_path",
             "before": [{"sleep": 0.3}, {"modify": {"gen.in": "changed\n"}}],
             "args": ["-f", "space build.ninja"],
             "expect": {"exit": 0, "files": ["out.txt"]}},
        ], description="Windows self-restart must preserve argument boundaries"),
    ])
