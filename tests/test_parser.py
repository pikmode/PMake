#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import subprocess
import tempfile
from pathlib import Path
import pytest

# Путь к pmake.exe (измени под свой)
PMAKE_BIN = r"E:\PikMode_apps\PikMode_Release\PS_Build\pmake.exe"


@pytest.fixture
def workdir():
    with tempfile.TemporaryDirectory() as tmp:
        yield Path(tmp)


def run_pmake(workdir, args=None):
    if args is None:
        args = []
    cmd = [PMAKE_BIN] + args
    result = subprocess.run(
        cmd,
        cwd=str(workdir),
        capture_output=True,
        text=True,
        timeout=10
    )
    return result


def test_simple_build(workdir):
    build_pmake = workdir / "build.pmake"
    build_pmake.write_text("""
pmake_required_version = 1.0
rule compile {
    command = "cmd /c copy ${in} ${out}"
}
build "out.txt" : compile "in.txt"
alias all : "out.txt"
""")
    (workdir / "in.txt").write_text("hello")
    result = run_pmake(workdir)
    assert result.returncode == 0, result.stderr
    assert (workdir / "out.txt").exists()
    assert (workdir / "out.txt").read_text() == "hello"


def test_plus_equals(workdir):
    build_pmake = workdir / "build.pmake"
    build_pmake.write_text("""
pmake_required_version = 1.0
cflags = "-Wall"
cflags += "-O2"
rule compile {
    command = "cmd /c echo ${cflags} > ${out}"
}
build "flags.txt" : compile
alias all : "flags.txt"
""")
    result = run_pmake(workdir)
    assert result.returncode == 0, result.stderr
    content = (workdir / "flags.txt").read_text().strip()
    assert content == "-Wall -O2"


def test_local_variables(workdir):
    build_pmake = workdir / "build.pmake"
    build_pmake.write_text("""
pmake_required_version = 1.0
cflags = "-Wall"
rule compile {
    command = "cmd /c echo ${cflags} > ${out}"
}
build "flags.txt" : compile {
    cflags = "-O2 -g"
}
alias all : "flags.txt"
""")
    result = run_pmake(workdir)
    assert result.returncode == 0, result.stderr
    content = (workdir / "flags.txt").read_text().strip()
    assert content == "-O2 -g"


if __name__ == "__main__":
    # Если запустить напрямую python test_parser.py, pytest не запустится.
    # Но можно использовать pytest.main()
    import pytest
    pytest.main([__file__, "-v"])