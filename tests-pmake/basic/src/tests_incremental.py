# -*- coding: utf-8 -*-
from ._common import test, pp


def _complex_incremental_graph(n=12):
    sources = [f"src/unit_{i:02d}.c" for i in range(n)]
    objects = [f"obj/unit_{i:02d}.o" for i in range(n)]
    object_args = " ".join(f'"{path}"' for path in objects)
    lines = [
        "pmake_required_version = 1.0",
        'python = "@PYTHON@"',
        "rule compile {",
        '    command = "${python} tracked_compile.py ${in} ${out}"',
        '    depfile = "${out}.d"',
        '    deps = "gcc"',
        "    restat = true",
        "}",
        "rule link {",
        '    command = "${python} tracked_link.py ${out} ${in}"',
        "    restat = true",
        "}",
    ]
    for source, output in zip(sources, objects):
        lines.append(f'build "{output}" : compile "{source}"')
    lines.extend([
        f'build "app" : link {object_args}',
        'alias all : "app"',
    ])
    files = {
        "build.pmake": "\n".join(lines) + "\n",
        "include/shared.h": "shared-v1\n",
        "unrelated.h": "untracked\n",
        "tracked_compile.py": '''import pathlib
import sys

source, output = map(pathlib.Path, sys.argv[1:3])
header = pathlib.Path("include") / f"{source.stem}.h"
shared = pathlib.Path("include/shared.h")
output.parent.mkdir(parents=True, exist_ok=True)
content = (
    source.read_text(encoding="utf-8")
    + header.read_text(encoding="utf-8")
    + shared.read_text(encoding="utf-8")
)
if not output.exists() or output.read_text(encoding="utf-8") != content:
    output.write_text(content, encoding="utf-8")
counter = pathlib.Path(str(output) + ".runs")
count = int(counter.read_text(encoding="utf-8")) if counter.exists() else 0
counter.write_text(str(count + 1), encoding="utf-8")
pathlib.Path(str(output) + ".d").write_text(
    f"{output}: {source} {header} {shared}\\n", encoding="utf-8"
)
''',
        "tracked_link.py": '''import pathlib
import sys

output = pathlib.Path(sys.argv[1])
inputs = [pathlib.Path(path) for path in sys.argv[2:]]
output.write_text(
    "".join(path.read_text(encoding="utf-8") for path in inputs),
    encoding="utf-8",
)
counter = pathlib.Path(str(output) + ".runs")
count = int(counter.read_text(encoding="utf-8")) if counter.exists() else 0
counter.write_text(str(count + 1), encoding="utf-8")
''',
    }
    files.update({path: f"source-{i:02d}\n" for i, path in enumerate(sources)})
    files.update({
        f"include/unit_{i:02d}.h": f"header-{i:02d}\n"
        for i in range(n)
    })
    return files, objects


_complex_graph_files, _complex_graph_objects = _complex_incremental_graph()

TESTS = [
    test("incr_source", {
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
        {"name": "modify_src", "before": [{"sleep": 0.5}, {"modify": {"src.c": "int v = 2;\n"}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.c": "int v = 2;"}}},
    ], description="Изменение src.c → out.c пересобирается"),

    test("incr_delete_output", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} z" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
        {"name": "delete_and_rebuild",
         "before": [{"sleep": 0.5}, {"delete": ["out.txt"]}],
         "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
    ], description="Удаление output → пересборка"),

    test("incr_chain", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
build "b.out" : copy "a.out"
build "c.out" : copy "b.out"
alias all : "c.out"
''',
        "a.in": "deep\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3,
                                     "files": ["a.out", "b.out", "c.out"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_a_in", "before": [{"sleep": 0.5}, {"touch": ["a.in"]}],
         "expect": {"exit": 0, "taskCount": 3}},
    ], description="a.in → a.out → b.out → c.out"),

    test("real_incr_chain_5", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
build "a.out" : copy "a.in"
build "b.out" : copy "a.out"
build "c.out" : copy "b.out"
build "d.out" : copy "c.out"
build "e.out" : copy "d.out"
alias all : "e.out"
''',
        "a.in": "deep5\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 5}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_a", "before": [{"sleep": 0.3}, {"touch": ["a.in"]}],
         "expect": {"exit": 0, "taskCount": 5}},
    ], description="5 уровней"),

    test("real_incr_diamond", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "b.out" : copy "a.in"
build "c.out" : copy "a.in"
build "d.out" : copy "b.out"
build "e.out" : copy "c.out"
build "f.out" : concat "d.out" "e.out"
alias all : "f.out"
''',
        "a.in": "diamond\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 5}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_root", "before": [{"sleep": 0.3}, {"touch": ["a.in"]}],
         "expect": {"exit": 0, "taskCount": 5}},
    ], description="Diamond граф"),

    test("real_incr_two_branches", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule concat { command = "${python} concat.py ${out} ${in}" }
build "x.o" : copy "x.c"
build "y.o" : copy "y.c"
build "app" : concat "x.o" "y.o"
alias all : "app"
''',
        "x.c": "x\n", "y.c": "y\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3}},
        {"name": "touch_x", "before": [{"sleep": 0.3}, {"touch": ["x.c"]}],
         "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_y", "before": [{"sleep": 0.3}, {"touch": ["y.c"]}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="Две ветки"),

    test("real_incr_cpp_header", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc {
    command = "${python} cc_make_dep.py ${in} ${out} common.h"
    depfile = "${out}.d"
    deps = "gcc"
}
rule link { command = "${python} concat.py ${out} ${in}" }
build "main.o" : cc "main.c"
build "util.o" : cc "util.c"
build "app" : link "main.o" "util.o"
alias all : "app"
''',
        "main.c": "main\n", "util.c": "util\n", "common.h": "// v1\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3}},
        {"name": "touch_h", "before": [{"sleep": 0.3}, {"touch": ["common.h"]}],
         "expect": {"exit": 0, "taskCount": 3}},
        {"name": "touch_main", "before": [{"sleep": 0.3}, {"touch": ["main.c"]}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="C++: .h → оба .o + app"),

    test("real_incr_partial_delete", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
''' + "\n".join(f'build "o_{i}.out" : copy "o_{i}.in"' for i in range(1, 11)) +
'\nalias all : ' + ", ".join(f'"o_{i}.out"' for i in range(1, 11))),
        **{f"o_{i}.in": f"data_{i}\n" for i in range(1, 11)},
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 10}},
        {"name": "delete_3",
         "before": [{"sleep": 0.3}, {"delete": ["o_3.out", "o_7.out", "o_9.out"]}],
         "expect": {"exit": 0, "taskCount": 3}},
    ], description="10 → удалили 3 → только 3 пересобираются"),

    test("real_incr_deep_10", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
''' + "\n".join(f'build "c{i}.out" : copy ' +
                ('"src.in"' if i == 0 else f'"c{i-1}.out"' ) for i in range(0, 10)) +
'\nalias all : "c9.out"'),
        "src.in": "deep10\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 10}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 10}},
    ], description="10-уровневая цепочка"),

    test("real_noop_and_incremental_under_fanout",
         _complex_graph_files, runs=[
        {"name": "initial_parallel_build",
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 13,
                    "files": _complex_graph_objects + ["app"],
                    "filesWith": {
                        "obj/unit_05.o.runs": "1",
                        "app.runs": "1",
                    }}},
        {"name": "repeated_full_build_is_a_true_noop",
         "before": [{"sleep": 0.3}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 0,
                    "filesWith": {
                        "obj/unit_05.o.runs": "1",
                        "app.runs": "1",
                    }}},
        {"name": "already_built_leaf_target_is_a_noop",
         "before": [{"sleep": 0.3}],
         "args": ["-f", "build.pmake", "obj/unit_05.o"],
         "expect": {"exit": 0, "taskCount": 0,
                    "filesWith": {"obj/unit_05.o.runs": "1"}}},
        {"name": "touch_one_source_rebuilds_only_that_compile",
         "before": [{"sleep": 0.3}, {"touch": ["src/unit_05.c"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 1,
                    "filesWith": {
                        "obj/unit_05.o.runs": "2",
                        "app.runs": "1",
                    }}},
        {"name": "touch_one_depfile_header_rebuilds_only_its_compile",
         "before": [{"sleep": 0.3}, {"touch": ["include/unit_05.h"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 1,
                    "filesWith": {
                        "obj/unit_05.o.runs": "3",
                        "app.runs": "1",
                    }}},
        {"name": "changed_private_header_relinks",
         "before": [{"sleep": 0.3}, {"modify": {
             "include/unit_05.h": "header-05-v2\n"}}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 2,
                    "filesWith": {
                        "obj/unit_05.o.runs": "4",
                        "app.runs": "2",
                    }}},
        {"name": "touch_shared_header_rebuilds_all_compiles_but_not_link",
         "before": [{"sleep": 0.3}, {"touch": ["include/shared.h"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 12,
                    "filesWith": {
                        "obj/unit_05.o.runs": "5",
                        "app.runs": "2",
                    }}},
        {"name": "changed_shared_header_rebuilds_graph",
         "before": [{"sleep": 0.3}, {"modify": {
             "include/shared.h": "shared-v2\n"}}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 13,
                    "filesWith": {
                        "obj/unit_05.o.runs": "6",
                        "app.runs": "3",
                    }}},
        {"name": "no_op_survives_cache_removal_and_untracked_changes",
         "before": [{"sleep": 0.3}, {"delete": ["Project-cache.pmake"]},
                    {"modify": {"unrelated.h": "changed but unused\n"}}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 0,
                    "filesWith": {
                        "obj/unit_05.o.runs": "6",
                        "app.runs": "3",
                    }}},
        {"name": "one_changed_source_rebuilds_only_its_path_to_app",
         "before": [{"sleep": 0.3}, {"modify": {
             "src/unit_02.c": "source-02-v2\n"}}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 2,
                    "filesWith": {
                        "obj/unit_02.o.runs": "4",
                        "app.runs": "4",
                    }}},
        {"name": "final_noop_keeps_invocation_counters_stable",
         "before": [{"sleep": 0.3}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 0,
                    "filesWith": {
                        "obj/unit_02.o.runs": "4",
                        "obj/unit_05.o.runs": "6",
                        "app.runs": "4",
                    }}},
    ], description="12-way depfile fanout: exact no-op counters, restat, cache loss, and minimal rebuilds",
       timeout=120),
]
