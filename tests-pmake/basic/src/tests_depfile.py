# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("depfile_basic", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule mk { command = "${python} make_dep.py ${out} hdr.h" depfile = "${out}.d" deps = "gcc" }
build "out.o" : mk "hdr.h"
alias all : "out.o"
''',
        "hdr.h": "/* v1 */\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.o"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_header", "before": [{"sleep": 0.5}, {"touch": ["hdr.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile: touch заголовка → пересборка (однострочный rule)"),

    test("depfile_unrelated", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule mk { command = "${python} make_dep.py ${out} hdr.h" depfile = "${out}.d" deps = "gcc" }
build "out.o" : mk "hdr.h"
alias all : "out.o"
''',
        "hdr.h": "/* v1 */\n", "other.h": "/* unrelated */\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "touch_unrelated",
         "before": [{"sleep": 0.5}, {"touch": ["other.h"]}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="depfile: touch нерелевантного → НЕ пересборка (однострочный rule)"),

    test("real_depfile_lost", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_make_dep.py ${in} ${out} common.h" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
''',
        "main.c": "main\n", "common.h": "// v1\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "lost_d_touch_h",
         "before": [{"sleep": 0.3}, {"delete": ["Project-cache.pmake", "main.o.d"]},
                    {"touch": ["common.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile lost → force rebuild (однострочный rule)"),

    test("real_depfile_persists", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} cc_make_dep.py ${in} ${out} hdr.h" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
''',
        "main.c": "main\n", "hdr.h": "// v1\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "touch_hdr", "before": [{"sleep": 0.3}, {"touch": ["hdr.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile persists (однострочный rule)"),

    test("depfile_missing_is_a_build_error", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} depfile_once.py ${out}" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
''',
        "main.c": "source\n",
        "depfile_once.py": '''import pathlib
import sys

out = pathlib.Path(sys.argv[1])
out.touch()
marker = pathlib.Path(".first-run")
if marker.exists():
    if marker.read_text(encoding="utf-8") == "skip":
        marker.write_text("retry", encoding="utf-8")
    else:
        pathlib.Path(str(out) + ".d").write_text(f"{out}: main.c\\n", encoding="utf-8")
else:
    marker.write_text("missing")
''',
    }, runs=[
        {"name": "missing_depfile_fails_and_removes_output",
         "expect": {"exit": 1, "taskCount": 1, "filesNot": ["main.o"],
                    "stderrHas": ["Declared depfile 'main.o.d' was not generated or is invalid"]}},
        {"name": "retry_generates_depfile",
         "expect": {"exit": 0, "taskCount": 1, "files": ["main.o", "main.o.d"]}},
        {"name": "valid_empty_or_nonempty_dep_log_is_cached",
         "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "stale_depfile_is_not_accepted",
         "before": [{"sleep": 0.3}, {"modify": {".first-run": "skip"}},
                    {"touch": ["main.c"]}],
         "expect": {"exit": 1, "taskCount": 1, "filesNot": ["main.o", "main.o.d"],
                    "stderrHas": ["Declared depfile 'main.o.d' was not generated or is invalid"]}},
        {"name": "retry_after_stale_depfile_failure",
         "expect": {"exit": 0, "taskCount": 1, "files": ["main.o", "main.o.d"]}},
        {"name": "retry_build_is_a_noop",
         "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="missing declared depfiles fail explicitly and allow a clean retry"),

    test("depfile_malformed_is_a_build_error", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} malformed_dep.py ${out}" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
''',
        "main.c": "source\n",
        "malformed_dep.py": '''import pathlib
import sys

out = pathlib.Path(sys.argv[1])
out.touch()
pathlib.Path(str(out) + ".d").write_text("this is not a depfile\\n", encoding="utf-8")
''',
    }, runs=[
        {"name": "malformed_depfile_fails_and_removes_output",
         "expect": {"exit": 1, "taskCount": 1, "filesNot": ["main.o"],
                    "stderrHas": ["Declared depfile 'main.o.d' was not generated or is invalid"]}},
    ], description="malformed declared depfiles are not silently accepted"),

    test("depfile_valid_without_dynamic_inputs_is_cached", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} empty_dep.py ${out}" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
''',
        "main.c": "source\n",
        "empty_dep.py": '''import pathlib
import sys

out = pathlib.Path(sys.argv[1])
out.touch()
pathlib.Path(str(out) + ".d").write_text(f"{out}:\\n", encoding="utf-8")
''',
    }, runs=[
        {"name": "valid_empty_depfile",
         "expect": {"exit": 0, "taskCount": 1, "files": ["main.o", "main.o.d"]}},
        {"name": "empty_dependency_list_is_persisted",
         "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="a valid depfile with an empty dependency list is distinct from a missing depfile"),

    test("depfile_dependency_set_refresh", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} select_dep.py ${in} ${out}" depfile = "${out}.d" deps = "gcc" }
build "main.o" : cc "main.c"
alias all : "main.o"
''',
        "main.c": "source v1\n",
        "selected.txt": "old.h\n",
        "old.h": "old dependency\n",
        "new.h": "new dependency\n",
        "select_dep.py": '''import pathlib
import sys

source, output = map(pathlib.Path, sys.argv[1:3])
dependency = pathlib.Path("selected.txt").read_text(encoding="utf-8").strip()
output.write_text(dependency, encoding="utf-8")
pathlib.Path(str(output) + ".d").write_text(
    f"{output}: {source} {dependency}\\n", encoding="utf-8"
)
''',
    }, runs=[
        {"name": "initially_tracks_old_header",
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"main.o": "old.h"}}},
        {"name": "source_change_replaces_dynamic_dependency",
         "before": [{"sleep": 0.3}, {"modify": {
             "selected.txt": "new.h\n", "main.c": "source v2\n"}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"main.o": "new.h"}}},
        {"name": "removed_header_no_longer_invalidates",
         "before": [{"sleep": 0.3}, {"touch": ["old.h"]}],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "new_header_invalidates_after_refresh",
         "before": [{"sleep": 0.3}, {"touch": ["new.h"]}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"main.o": "new.h"}}},
        {"name": "refreshed_dependency_state_is_a_noop",
         "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="replacing depfile inputs removes stale graph edges and tracks new headers"),

    test("dep_msvc_show_includes_and_warnings", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} fake_msvc.py /showIncludes ${out}" deps = "msvc" }
build "main.obj" : cc "main.c"
alias all : "main.obj"
''',
        "main.c": "source\n",
        "include dir/header one.h": "outer\n",
        "include dir/nested header.h": "inner\n",
        "unrelated.h": "unrelated\n",
        "fake_msvc.py": '''import pathlib
import sys

out = pathlib.Path(sys.argv[-1])
out.touch()
print("  Note: including file: include dir/header one.h")
sys.stderr.buffer.write(
    "    Примечание: включение файла: include dir/nested header.h\\n".encode("utf-8")
)
print("warning: test compiler warning", file=sys.stderr)
''',
    }, runs=[
        {"name": "first_build_captures_both_streams",
         "expect": {"exit": 0, "taskCount": 1,
                    "stderrHas": ["warning: test compiler warning"],
                    "stderrNot": ["Note: including file:",
                                  "Примечание: включение файла:"],
                    "stdoutNot": ["Note: including file:"],
                    "files": ["main.obj"]}},
        {"name": "cached_dependencies_make_noop",
         "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_nested_include_rebuilds",
         "before": [{"sleep": 0.3}, {"touch": ["include dir/nested header.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "unrelated_header_does_not_rebuild",
         "before": [{"sleep": 0.3}, {"touch": ["unrelated.h"]}],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_outer_include_rebuilds",
         "before": [{"sleep": 0.3}, {"touch": ["include dir/header one.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "deleted_include_forces_rebuild",
         "before": [{"sleep": 0.3}, {"delete": ["include dir/nested header.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="MSVC /showIncludes from stdout/stderr is captured, persisted, and invalidates on header changes"),

    test("successful_command_shows_compiler_warnings", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} warning_build.py ${out}" }
build "out.o" : cc "in.c"
alias all : "out.o"
''',
        "in.c": "source\n",
        "warning_build.py": '''import pathlib
import sys

pathlib.Path(sys.argv[1]).touch()
print("warning: visible compiler warning", file=sys.stderr)
print("Предупреждение: кириллица отображается", file=sys.stderr)
''',
    }, runs=[
        {"name": "warning_is_forwarded_on_success",
         "expect": {"exit": 0, "taskCount": 1,
                    "stderrHasExactlyOnce": ["warning: visible compiler warning",
                                             "Предупреждение: кириллица отображается"]}},
    ], description="successful commands forward compiler warnings and UTF-8 Cyrillic output"),

    # NEW: bug #3 — `:` в Windows drive-letter путях не должен ломать парсер.
    # Старый парсер на "C:\build\main.o: main.c C:\src\hdr.h" выдавал:
    #   "\build\main.o main.c C \src\hdr.h"
    # Новый — "main.c C:\src\hdr.h". Проверяем через builtin dep_inputs().
    test("real_dep_parser_drive_letter", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
inputs = dep_inputs("test.d")
rule write { command = "${python} write_all.py ${out} ${inputs}" }
build "out.txt" : write
alias all : "out.txt"
''',
        "test.d": "C:\\build\\main.o: main.c C:\\src\\hdr.h\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "main.c C:\\src\\hdr.h"}}},
    ], description="dep_parser: drive-letter пути в target и inputs"),

    # NEW: дополнительно — drive letter в target, обычные inputs.
    test("real_dep_parser_drive_letter_target", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
inputs = dep_inputs("test.d")
rule write { command = "${python} write_all.py ${out} ${inputs}" }
build "out.txt" : write
alias all : "out.txt"
''',
        "test.d": "C:\\out\\main.o: a.h b.h\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "a.h b.h"}}},
    ], description="dep_parser: drive-letter в target, обычные inputs"),
]