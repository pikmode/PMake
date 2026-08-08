import pytest
import subprocess
import tempfile
import os
import shutil
import time
from pathlib import Path

# ============================================================
# Конфигурация
# ============================================================

PMAKE_BIN = r"E:\PikMode_apps\PikMode_Release\PS_Build\pmake.exe"

# ============================================================
# Вспомогательные функции
# ============================================================

def write_build_pmake(path, content):
    (path / "build.pmake").write_text(content, encoding="utf-8")

def run_pmake(project_dir, args=None, expect_fail=False):
    if args is None:
        args = []
    cmd = [PMAKE_BIN] + args
    result = subprocess.run(
        cmd,
        cwd=project_dir,
        capture_output=True,
        text=True,
        shell=False
    )
    if not expect_fail:
        assert result.returncode == 0, f"PMake failed: {result.stderr}"
    return result

# ============================================================
# Фикстуры
# ============================================================

@pytest.fixture
def tmp_project(tmp_path):
    old_cwd = os.getcwd()
    os.chdir(tmp_path)
    yield tmp_path
    os.chdir(old_cwd)

# ============================================================
# Тесты (Базовые)
# ============================================================

class TestBasic:
    def test_simple_build(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
    description = "Copying ${in}"
}
build "out.txt" : compile "in.txt"
alias all : "out.txt"
""")
        (tmp_project / "in.txt").write_text("hello")
        run_pmake(tmp_project)
        assert (tmp_project / "out.txt").exists()
        assert (tmp_project / "out.txt").read_text() == "hello"

    def test_incremental_no_change(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
    description = "Copying ${in}"
}
build "out.txt" : compile "in.txt"
alias all : "out.txt"
""")
        (tmp_project / "in.txt").write_text("hello")
        run_pmake(tmp_project)
        result = run_pmake(tmp_project)
        assert "Copying" not in result.stdout

    def test_incremental_source_changed(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
    description = "Copying ${in}"
}
build "out.txt" : compile "in.txt"
alias all : "out.txt"
""")
        (tmp_project / "in.txt").write_text("hello")
        run_pmake(tmp_project)
        (tmp_project / "in.txt").write_text("world")
        result = run_pmake(tmp_project)
        assert "Copying in.txt" in result.stdout
        assert (tmp_project / "out.txt").read_text() == "world"

    def test_plus_equals(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
cflags = "-Wall"
cflags += "-O2"
rule compile {
    command = "echo ${cflags} > ${out}"
}
build "flags.txt" : compile
alias all : "flags.txt"
""")
        run_pmake(tmp_project)
        content = (tmp_project / "flags.txt").read_text().strip()
        assert content == "-Wall -O2"

    def test_local_variables(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
cflags = "-Wall"
rule compile {
    command = "echo ${cflags} > ${out}"
}
build "flags.txt" : compile {
    cflags = "-O2 -g"
}
alias all : "flags.txt"
""")
        run_pmake(tmp_project)
        content = (tmp_project / "flags.txt").read_text().strip()
        assert content == "-O2 -g"

# ============================================================
# Тесты (Параллелизм)
# ============================================================

class TestParallelism:
    def test_parallel_build(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "echo ${in} > ${out}"
}
build "a.txt" : compile "a.in"
build "b.txt" : compile "b.in"
build "c.txt" : compile "c.in"
alias all : "a.txt" "b.txt" "c.txt"
""")
        (tmp_project / "a.in").write_text("a")
        (tmp_project / "b.in").write_text("b")
        (tmp_project / "c.in").write_text("c")
        run_pmake(tmp_project, ["-j", "4"])
        assert (tmp_project / "a.txt").exists()
        assert (tmp_project / "b.txt").exists()
        assert (tmp_project / "c.txt").exists()

    def test_console_pool(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "timeout /t 1 >nul & echo ${in} > ${out}"
    pool = "console"
}
build "a.txt" : compile "a.in"
build "b.txt" : compile "b.in"
alias all : "a.txt" "b.txt"
""")
        (tmp_project / "a.in").write_text("a")
        (tmp_project / "b.in").write_text("b")
        start = time.time()
        run_pmake(tmp_project, ["-j", "4"])
        duration = time.time() - start
        # При pool=console задачи выполняются последовательно, должно быть > 2 сек (2 задачи по 1 сек)
        assert duration > 1.8

# ============================================================
# Тесты (Кеш)
# ============================================================

class TestCache:
    def test_cache_creation(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
}
build "out.txt" : compile "in.txt"
alias all : "out.txt"
""")
        (tmp_project / "in.txt").write_text("hello")
        run_pmake(tmp_project)
        cache_file = tmp_project / "Project-cache.pmake"
        assert cache_file.exists()

    def test_cache_reuse(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
    description = "Copying ${in}"
}
build "out.txt" : compile "in.txt"
alias all : "out.txt"
""")
        (tmp_project / "in.txt").write_text("hello")
        run_pmake(tmp_project)
        result = run_pmake(tmp_project)
        assert "Copying" not in result.stdout

    def test_cache_invalid_when_buildfile_changed(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
}
build "out.txt" : compile "in.txt"
alias all : "out.txt"
""")
        (tmp_project / "in.txt").write_text("hello")
        run_pmake(tmp_project)
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
}
build "out2.txt" : compile "in.txt"
alias all : "out2.txt"
""")
        run_pmake(tmp_project)
        assert (tmp_project / "out2.txt").exists()

# ============================================================
# Тесты (Ошибки)
# ============================================================

class TestErrorHandling:
    def test_bad_command(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "nonexistent_command"
}
build "out.txt" : compile
alias all : "out.txt"
""")
        result = subprocess.run(
            [PMAKE_BIN, "all"],
            cwd=tmp_project,
            capture_output=True,
            text=True,
            shell=False
        )
        assert result.returncode != 0
        assert "Program not found" in result.stderr or "nonexistent_command" in result.stderr

    def test_cycle_detection(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
}
build "a.txt" : compile "b.txt"
build "b.txt" : compile "a.txt"
alias all : "a.txt"
""")
        (tmp_project / "a.txt").write_text("")
        (tmp_project / "b.txt").write_text("")
        result = subprocess.run(
            [PMAKE_BIN, "all"],
            cwd=tmp_project,
            capture_output=True,
            text=True,
            shell=False
        )
        assert result.returncode != 0
        assert "Cycle" in result.stderr or "cycle" in result.stderr.lower()

# ============================================================
# Тесты (Продвинутые возможности)
# ============================================================

class TestAdvancedFeatures:
    def test_multiple_outputs(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "echo hello > ${out}"
}
build "a.txt" "b.txt" : compile
alias all : "a.txt" "b.txt"
""")
        run_pmake(tmp_project)
        assert (tmp_project / "a.txt").exists()
        assert (tmp_project / "b.txt").exists()

    def test_alias(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
}
build "target.txt" : compile "input.txt"
alias myalias : "target.txt"
""")
        (tmp_project / "input.txt").write_text("data")
        run_pmake(tmp_project, ["myalias"])
        assert (tmp_project / "target.txt").exists()

    def test_depfile_support(self, tmp_project):
        write_build_pmake(tmp_project, """
pmake_required_version = 1.0
rule compile {
    command = "copy ${in} ${out}"
    description = "Compiling ${in}"
    depfile = "${out}.d"
}
build "main.o" : compile "main.c"
alias all : "main.o"
""")
        (tmp_project / "main.c").write_text("int main() { return 0; }")
        (tmp_project / "main.o.d").write_text("main.o: main.c header.h")
        (tmp_project / "header.h").write_text("// header")
        run_pmake(tmp_project)
        time.sleep(0.1)
        (tmp_project / "header.h").write_text("// changed")
        result = run_pmake(tmp_project)
        assert "Compiling main.c" in result.stdout