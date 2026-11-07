# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_rspfile_basic", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule gen {
    command = "${python} cat_rspfile.py ${rspfile} ${out}"
    rspfile = "${out}.rsp"
    rspfile_content = "line1\\nline2\\n"
}
build "out.txt" : gen
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"],
                                     "filesWith": {"out.txt": "line1\nline2"}}},
        {"name": "rspfile_removed", "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0, "filesNot": ["out.txt.rsp"]}},
    ], description="rspfile: пишется перед командой, удаляется после"),
]
