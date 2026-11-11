# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("restat", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule touch_always {
    command = "${python} restat.py ${out}"
    restat = true
}
rule copy {
    command = "${python} copy.py ${in} ${out}"
}
build "mid.txt" : touch_always
build "final.txt" : copy "mid.txt"
alias all : "final.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2,
                                     "files": ["mid.txt", "final.txt"]}},
        {"name": "noop_restat", "before": [{"sleep": 0.5}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="restat=true: touch → content unchanged → final skip"),

    test("real_restat_skip", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
build "mid.txt" : cp_preserve "src.in"
build "final.txt" : cp_preserve "mid.txt"
alias all : "final.txt"
''',
        "src.in": "constant\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="restat: touch src → final пропускается"),

    test("real_restat_chain_3", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
build "m1.txt" : cp_preserve "src.in"
build "m2.txt" : cp_preserve "m1.txt"
build "m3.txt" : cp_preserve "m2.txt"
alias all : "m3.txt"
''',
        "src.in": "chain3\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="restat chain 3"),

    test("real_restat_content_change", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
build "mid.txt" : cp_preserve "src.in"
build "final.txt" : cp_preserve "mid.txt"
alias all : "final.txt"
''',
        "src.in": "v1\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "modify_src", "before": [{"sleep": 0.3}, {"modify": {"src.in": "v2\n"}}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="restat: контент изменился → оба пересобираются"),

    test("real_restat_multi_output", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_mp {
    command = "${python} copy_preserve_multi.py ${in} ${out}"
    restat = true
}
rule concat {
    command = "${python} concat.py ${out} ${in}"
}
build "a.txt", "b.txt" : gen_mp "src.in"
build "final.txt" : concat "a.txt" "b.txt"
alias all : "final.txt"
''',
        "src.in": "multi_restat\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="restat + multi-output"),

    test("real_restat_with_depfile", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc {
    command = "${python} cc_make_dep.py ${in} ${out} common.h"
    depfile = "${out}.d"
    deps = "gcc"
    restat = true
}
rule link {
    command = "${python} concat.py ${out} ${in}"
}
build "main.o" : cc "main.c"
build "app" : link "main.o"
alias all : "app"
''',
        "main.c": "main\n", "common.h": "// v1\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_h", "before": [{"sleep": 0.3}, {"touch": ["common.h"]}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="restat + depfile"),

    test("real_restat_no_flag", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = false
}
build "mid.txt" : cp_preserve "src.in"
build "final.txt" : cp_preserve "mid.txt"
alias all : "final.txt"
''',
        "src.in": "constant\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 2}},
    ], description="restat=false: downstream всё равно rebuild"),

    test("real_restat_deep_5", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_preserve {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
build "c0.txt" : cp_preserve "src.in"
build "c1.txt" : cp_preserve "c0.txt"
build "c2.txt" : cp_preserve "c1.txt"
build "c3.txt" : cp_preserve "c2.txt"
build "c4.txt" : cp_preserve "c3.txt"
alias all : "c4.txt"
''',
        "src.in": "deep5\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 5}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="restat chain 5"),

    test("real_restat_mixed", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cp_restat {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
rule cp_plain {
    command = "${python} copy_preserve.py ${in} ${out}"
}
build "a.txt" : cp_restat "src.in"
build "b.txt" : cp_plain "a.txt"
alias all : "b.txt"
''',
        "src.in": "mixed\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2}},
        {"name": "touch_src", "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="A(restat) → B(no restat)"),
]