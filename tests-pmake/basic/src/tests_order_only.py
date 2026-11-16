# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_implicit_basic", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen {
    command = "${python} concat.py ${out} ${in}"
}
build "out.txt" : gen "main.txt" | "hdr.txt"
alias all : "out.txt"
''',
        "main.txt": "main\n", "hdr.txt": "hdr\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_implicit", "before": [{"sleep": 0.3}, {"touch": ["hdr.txt"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="implicit: touch hdr → out rebuild"),

    test("real_order_only_basic", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy {
    command = "${python} copy.py ${in} ${out}"
}
build "stamp.txt" : copy "stamp.in"
build "out.txt" : copy "out.in" || "stamp.txt"
alias all : "out.txt"
''',
        "stamp.in": "stamp\n", "out.in": "out\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_order_only", "before": [{"sleep": 0.3}, {"touch": ["stamp.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="order-only: touch stamp → stamp rebuild, out skip"),

    test("real_order_only_waits", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule concat {
    command = "${python} concat.py ${out} ${in}"
}
rule gen_stamp {
    command = "${python} echo_to.py ${out} stamp_v1"
}
build "stamp.txt" : gen_stamp
build "out.txt" : concat "in.txt" || "stamp.txt"
alias all : "out.txt"
''',
        "in.txt": "hello\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
    ], description="order-only: out ждёт stamp"),

    test("real_order_only_chain", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy {
    command = "${python} copy.py ${in} ${out}"
}
build "s1.txt" : copy "s1.in"
build "s2.txt" : copy "s2.in" || "s1.txt"
build "out.txt" : copy "out.in" || "s2.txt"
alias all : "out.txt"
''',
        "s1.in": "s1\n", "s2.in": "s2\n", "out.in": "out\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3, "files": ["s1.txt", "s2.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_s1", "before": [{"sleep": 0.3}, {"touch": ["s1.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="order-only chain"),

    test("real_order_only_restat", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_restat {
    command = "${python} copy_preserve.py ${in} ${out}"
    restat = true
}
rule check_stamp {
    command = "${python} check_stamp.py stamp.txt ${in} ${out}"
}
build "stamp.txt" : gen_restat "stamp.in"
build "out.txt" : check_stamp "out.in" || "stamp.txt"
alias all : "out.txt"
''',
        "stamp.in": "constant\n", "out.in": "out\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_stamp_skip", "before": [{"sleep": 0.3}, {"touch": ["stamp.in"]}],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "modify_stamp_order_only_no_propagate",
         "before": [{"sleep": 0.3}, {"modify": {"stamp.in": "changed\n"}}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="order-only + restat"),

    test("real_order_only_generator", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_once {
    command = "${python} touch.py ${out}"
    generator = true
}
rule check_stamp {
    command = "${python} check_stamp.py stamp.txt ${in} ${out}"
}
build "stamp.txt" : gen_once
build "out.txt" : check_stamp "out.in" || "stamp.txt"
alias all : "out.txt"
''',
        "out.in": "out\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_stamp", "before": [{"sleep": 0.3}, {"delete": ["stamp.txt"]}],
         "expect": {"exit": 0, "taskCount": 1, "files": ["stamp.txt", "out.txt"]}},
    ], description="order-only + generator"),

    test("real_order_only_missing", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy {
    command = "${python} copy.py ${in} ${out}"
}
build "out.txt" : copy "in.txt" || "nonexistent.stamp"
alias all : "out.txt"
''',
        "in.txt": "in\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"],
                                     "filesWith": {"out.txt": "in"}}},
    ], description="missing order-only: leaf игнорируется"),

    test("real_order_only_multi", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule slow_touch {
    command = "${python} sleep_and_touch.py 300 ${out}"
}
rule check_two {
    command = "${python} check_two.py s1.txt s2.txt ${out}"
}
build "s1.txt" : slow_touch
build "s2.txt" : slow_touch
build "out.txt" : check_two || "s1.txt" "s2.txt"
alias all : "out.txt"
''',
    }, runs=[
        {"name": "j2_ordered", "args": ["-f", "build.pmake", "-j", "2"],
         "expect": {"exit": 0, "taskCount": 3, "files": ["s1.txt", "s2.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.5}], "expect": {"exit": 0, "taskCount": 0}},
    ], description="multi order-only"),

    test("real_order_only_and_implicit", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen_out {
    command = "${python} concat.py ${out} ${in}"
}
rule gen_stamp {
    command = "${python} touch.py ${out}"
}
build "stamp.txt" : gen_stamp
build "out.txt" : gen_out "main.txt" | "hdr.txt" || "stamp.txt"
alias all : "out.txt"
''',
        "main.txt": "main\n", "hdr.txt": "hdr\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 2, "files": ["stamp.txt", "out.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}], "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_implicit", "before": [{"sleep": 0.3}, {"touch": ["hdr.txt"]}],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "delete_stamp", "before": [{"sleep": 0.3}, {"delete": ["stamp.txt"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="implicit + order-only"),
]