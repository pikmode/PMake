# -*- coding: utf-8 -*-
"""
Critical regression-тесты для v1.3.0.

Два data-destroying бага, найденных в реальной работе:

1. `pmake -t clean` удалял source-файлы и CMake-инсталляцию.
   CMake генерирует phony-edge ("missing input files is not an error"),
   где все входные файлы стоят в позиции OUTPUTS. Без пропуска phony
   `pmake -t clean` удалял CMakeLists.txt, CMakeCache.txt и 200+ .cmake
   модулей установки CMake.

2. Windows drive letter `E$:` в ninja-парсере.
   `parseName` fast-path обрывал `E:/pikmode/...` на "E" →
   "Duplicate output 'E'" × N при парсинге CMake-generated build.ninja.
"""
from ._common import test


TESTS = [
    # ============================================================
    # CRITICAL: clean не должен удалять inputs из phony-edge.
    #
    # CHANGED: `echo $out` → `@PYTHON@ touch.py $out`.
    # `echo` — builtin cmd.exe, не .exe. CreateProcess("echo", ...)
    # падает с "Failed to start process". touch.py — реальный .exe
    # (Python), создаёт out.o через pathlib.touch().
    # ============================================================
    test("clean_protection_phony_inputs", {
        "build.ninja": '''rule cc
  command = @PYTHON@ touch.py $out
rule phony_rule
  command =

build source.cpp header.h CMakeLists.txt : phony_rule
build out.o : cc source.cpp
default out.o
''',
        "source.cpp": "int main(){return 0;}\n",
        "header.h": "// header\n",
        "CMakeLists.txt": "# CMake\n",
    }, runs=[
        {"name": "build_first", "args": ["-f", "build.ninja"],
         "expect": {"exit": 0, "files": ["out.o"]}},
        {"name": "clean_keeps_sources",
         "args": ["-f", "build.ninja", "-t", "clean"],
         "expect": {
             "exit": 0,
             # source-файлы должны выжить после clean
             "files": ["source.cpp", "header.h", "CMakeLists.txt"],
             # out.o удалён (это настоящий output cc-rule)
             "filesNot": ["out.o"],
         }},
    ], description="CRITICAL: -t clean не удаляет inputs из phony",
       timeout=60),

    # ============================================================
    # CRITICAL: Windows drive letter в ninja inputs (E$:/...).
    #
    # CMake генерирует абсолютные пути как E$:/path (escape $:).
    # Старый fast-path обрывал "E" → inputs становились "E", ":",
    # "/path", что давало "Duplicate output 'E'" при большом
    # количестве билдов.
    #
    # CHANGED: `command = echo $in` → `command = @PYTHON@ touch.py $out`
    # (та же причина: echo не executable). Тест делает parse + dry-run,
    # так что команда не выполняется — но для единообразия.
    # ============================================================
    test("ninja_drive_letter_input", {
        "build.ninja": '''rule cc
  command = @PYTHON@ touch.py $out

build out.o : cc E$:/pikmode/project/src.cpp E$:/pikmode/project/header.h
default out.o
''',
    }, runs=[
        {"name": "parse_without_duplicate_e",
         "args": ["-f", "build.ninja", "-n"],
         "expect": {
             "exit": 0,
             # Если E$: обрывается — parser выдаст duplicate 'E'
             # или errors про невалидный edge. taskCount=1 — правильно
             # (только out.o в плане).
             "taskCountMax": 5,
         }},
    ], description="CRITICAL: E$: escape в ninja inputs (Windows drive letter)",
       timeout=60),

    test("successful_command_shows_compiler_warnings", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc { command = "${python} warning_build.py ${out}" }
build "out.o" : cc "in.c"
alias all : "out.o"
''',
        "in.c": "source\n",
        "warning_build.py": '''import pathlib
import sys

# FIX: stderr по умолчанию использует ACP системы (GetACP()).
# На русской Windows ACP=1251 — кириллица кодируется в cp1251,
# и pmake (windows_output_to_utf8) корректно её декодирует обратно.
# На English Windows (GitHub Actions) ACP=1252 — кириллицы в cp1252 нет,
# Python по PEP 528 применяет errors='backslashreplace' и превращает
# каждый символ в escape "\\uXXXX", чистый ASCII — pmake пропускает
# как валидный UTF-8, и кириллица до runner.py не доходит.
# Явно переключаем stderr на UTF-8, чтобы результат не зависел от локали.
sys.stderr.reconfigure(encoding="utf-8")

pathlib.Path(sys.argv[1]).touch()
print("warning: visible compiler warning", file=sys.stderr)
print("Предупреждение: кириллица отображается", file=sys.stderr)
''',
    }, runs=[
        {"name": "warning_is_forwarded_on_success",
         "expect": {"exit": 0, "taskCount": 1,
                    "stderrHasExactlyOnce": ["warning: visible compiler warning",
                                             "Предупреждение: кириллица отображается"]}},
    ], description="successful commands forward compiler warnings and UTF-8 Cyrillic output"),

    # NEW: bug #3 — `:` в Windows drive-letter путях не должен ломать парсер.
    # Старый парсер на "C:\build\main.o: main.c C:\src\hdr.h" выдавал:
    #   "\build\main.o main.c C \src\hdr.h"
    # Новый — "main.c C:\src\hdr.h". Проверяем через builtin dep_inputs().
    test("real_dep_parser_drive_letter", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
inputs = dep_inputs("test.d")
rule write { command = "${python} write_all.py ${out} ${inputs}" }
build "out.txt" : write
alias all : "out.txt"
''',
        "test.d": "C:\\build\\main.o: main.c C:\\src\\hdr.h\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "main.c C:\\src\\hdr.h"}}},
    ], description="dep_parser: drive-letter пути в target и inputs"),

    # NEW: дополнительно — drive letter в target, обычные inputs.
    test("real_dep_parser_drive_letter_target", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
inputs = dep_inputs("test.d")
rule write { command = "${python} write_all.py ${out} ${inputs}" }
build "out.txt" : write
alias all : "out.txt"
''',
        "test.d": "C:\\out\\main.o: a.h b.h\n",
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "a.h b.h"}}},
    ], description="dep_parser: drive-letter в target, обычные inputs"),
]