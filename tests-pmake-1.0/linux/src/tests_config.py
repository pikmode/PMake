# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("real_config_default_release_linux", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
config debug {
    build_mode = "DEBUG_MODE"
}
config release {
    build_mode = "RELEASE_MODE"
}
rule echo { command = "${python} write_all.py ${out} ${build_mode}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        # Без флага --config должен примениться release (дефолт)
        {"name": "default", "expect": {"exit": 0, "filesWith": {"out.txt": "RELEASE_MODE"}}},
        # С флагом --config debug должен примениться debug
        {"name": "debug", "args": ["-f", "build.pmake", "--config", "debug"],
         "expect": {"exit": 0, "filesWith": {"out.txt": "DEBUG_MODE"}}},
    ], description="Config debug/release applied")
]
