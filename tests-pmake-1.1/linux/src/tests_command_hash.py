# -*- coding: utf-8 -*-
from ._common import test, pp

TESTS = [
    test("real_command_change", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} -O2" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "-O2"}}},
        {"name": "change_command",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} -O3" }
build "out.txt" : echo
alias all : "out.txt"
''')}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "-O3"}}},
    ], description="command hash: -O2 → -O3 → rebuild"),

    test("real_command_var_change", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
flags = "-O2"
rule echo { command = "${python} echo_to.py ${out} ${flags}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1}},
        {"name": "change_var",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
flags = "-O3"
rule echo { command = "${python} echo_to.py ${out} ${flags}" }
build "out.txt" : echo
alias all : "out.txt"
''')}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"out.txt": "-O3"}}},
    ], description="command hash: flags -O2 → -O3"),

    test("real_command_hash_chain_3", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule with_flag { command = "${python} echo_to.py ${out} FLAG_V1" }
build "a.out" : copy "src.in"
build "b.out" : copy "a.out"
build "c.out" : with_flag "b.out"
alias all : "c.out"
'''),
        "src.in": "chain\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 3, "filesWith": {"c.out": "FLAG_V1"}}},
        {"name": "change_c_only",
         "before": [{"sleep": 0.3}, {"modify": {"build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule copy { command = "${python} copy.py ${in} ${out}" }
rule with_flag { command = "${python} echo_to.py ${out} FLAG_V2" }
build "a.out" : copy "src.in"
build "b.out" : copy "a.out"
build "c.out" : with_flag "b.out"
alias all : "c.out"
''')}}],
         "expect": {"exit": 0, "taskCount": 1, "filesWith": {"c.out": "FLAG_V2"}}},
    ], description="command hash chain"),
]
