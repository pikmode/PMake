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
]