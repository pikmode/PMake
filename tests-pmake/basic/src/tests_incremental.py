# -*- coding: utf-8 -*-
from ._common import test, pp

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
]
