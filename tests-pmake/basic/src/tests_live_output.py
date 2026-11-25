# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("command_output_is_streamed_live", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule stream { command = "${python} stream_until_released.py ${out} release" }
build "out.txt" : stream
alias all : "out.txt"
''',
        "stream_until_released.py": '''import pathlib
import sys
import time

output = pathlib.Path(sys.argv[1])
release = pathlib.Path(sys.argv[2])
print("live output before command exits", flush=True)
print("live error before command exits", file=sys.stderr, flush=True)
deadline = time.monotonic() + 10
while not release.exists() and time.monotonic() < deadline:
    time.sleep(0.01)
print("live output after release", flush=True)
output.write_text("done", encoding="utf-8")
''',
    }, runs=[
        {"name": "output_arrives_while_pmake_is_running",
         "streaming": {"stdoutMarker": "live output before command exits",
                       "stderrMarker": "live error before command exits",
                       "releaseFile": "release"},
         "expect": {"exit": 0, "taskCount": 1, "outputBeforeExit": True,
                    "stdoutHas": ["live output before command exits",
                                  "live output after release"],
                    "stderrHas": ["live error before command exits"],
                    "filesWith": {"out.txt": "done"}}},
    ], description="command output is forwarded before the child process exits"),
]
