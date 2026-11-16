# -*- coding: utf-8 -*-
from ._common import test, pp

TESTS = [
    # 1. Проверка валидации -j
    test("cli_jobs_invalid", {
        "build.pmake": 'pmake_required_version = 1.0\nalias all :\n',
    }, runs=[
        {"name": "j_zero", "args": ["-f", "build.pmake", "-j", "0"], 
         "expect": {"exit": 1, "stderrHas": ["error: invalid value '0' for '-j'"]}},
        {"name": "j_negative", "args": ["-f", "build.pmake", "-j", "-5"], 
         "expect": {"exit": 1, "stderrHas": ["error: invalid value '-5' for '-j'"]}},
        {"name": "j_abc", "args": ["-f", "build.pmake", "-j", "abc"], 
         "expect": {"exit": 1, "stderrHas": ["error: invalid value 'abc' for '-j'"]}},
        {"name": "j_missing_value", "args": ["-f", "build.pmake", "-j"], 
         "expect": {"exit": 1, "stderrHas": ["error: option '-j' requires an argument"]}},
    ], description="Валидация флага -j"),

    # 2. Проверка Ctrl+C
    test("cli_ctrl_c_interrupt", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule slow {
    command = "${python} sleep_and_touch.py 2000 ${out}"
}
build "out.txt" : slow
alias all : "out.txt"
''',
    }, runs=[
        {"name": "interrupt", "args": ["-f", "build.pmake"], 
         "kill_after_ms": 500, "continue": True, 
         "expect": {"stderrHas": ["error: Build interrupted"]}},
    ], description="Ctrl+C: корректный выход с сообщением"),

    # 3. Проверка Ninja-парсера на Windows Drive Letter
    test("ninja_drive_letter", {
        "build.ninja": '''rule echo
  command = echo $out
build "C:/test/out.txt" : echo
''',
    }, runs=[
        {"name": "dry", "args": ["-f", "build.ninja", "-n"], 
         "expect": {"exit": 0, "stdoutHas": ["C:/test/out.txt"]}},
    ], description="Ninja parser: Windows drive letter в путях"),

    # 4. Нагрузочный тест DepsLog
    test("deps_log_concurrency_stress", {
        "build.pmake": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule cc {
    command = "${python} make_dep.py ${out} hdr.h"
    depfile = "${out}.d"
    deps = "gcc"
}
''' + "\n".join(f'build "f_{i:02d}.o" : cc "f_{i:02d}.c"' for i in range(1, 51)) +
'\nalias all : ' + ", ".join(f'"f_{i:02d}.o"' for i in range(1, 51))),
        **{f"f_{i:02d}.c": f"int x_{i};\n" for i in range(1, 51)},
        "hdr.h": "// header\n"
    }, runs=[
        {"name": "j16", "args": ["-f", "build.pmake", "-j", "16"], 
         "expect": {"exit": 0, "taskCount": 50}},
        {"name": "noop", "before": [{"sleep": 0.5}], 
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="DepsLog concurrency (50 depfiles)"),

    # 5a. Регенерация манифеста (Ninja-style)
    test("real_manifest_regeneration_ninja", {
        "build.ninja": '''rule regen
  command = @PYTHON@ copy.py gen.in build.ninja
  generator = 1
build build.ninja : regen gen.in

rule touch
  command = @PYTHON@ touch.py $out

build out1.txt : touch
default out1.txt
''',
        "gen.in": '''rule regen
  command = @PYTHON@ copy.py gen.in build.ninja
  generator = 1
build build.ninja : regen gen.in

rule touch
  command = @PYTHON@ touch.py $out

build out2.txt : touch
default out2.txt
''',
    }, runs=[
        {"name": "regen_1", "args": ["-f", "build.ninja"],
         "before": [{"sleep": 1.2}, {"touch": ["gen.in"]}],
         "expect": {"exit": 0, "files": ["out2.txt"], "filesNot": ["out1.txt"],
                    "stderrHas": ["Build manifest was regenerated"]}},
        {"name": "noop", "args": ["-f", "build.ninja"],
         "before": [{"sleep": 1.2}], 
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "regen_2", "args": ["-f", "build.ninja"],
         "before": [
            {"sleep": 1.2}, 
            {"modify": {"gen.in": pp('''rule regen
  command = @PYTHON@ copy.py gen.in build.ninja
  generator = 1
build build.ninja : regen gen.in

rule touch
  command = @PYTHON@ touch.py $out

build out3.txt : touch
default out3.txt
''')}}
        ], "expect": {"exit": 0, "files": ["out3.txt"], 
                      "stderrHas": ["Build manifest was regenerated"]}},
    ], description="Ninja-style manifest regeneration (3 scenarios)", args=["-f", "build.ninja"]),

    # 5b. Регенерация манифеста (PMake-style)
    test("real_manifest_regeneration_pmake", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule regen {
    command = "${python} copy.py gen.in build.pmake"
    generator = true
}
build "build.pmake" : regen "gen.in"

rule touch {
    command = "${python} touch.py ${out}"
}
build "out1.txt" : touch
alias all : "out1.txt"
''',
        "gen.in": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule regen {
    command = "${python} copy.py gen.in build.pmake"
    generator = true
}
build "build.pmake" : regen "gen.in"

rule touch {
    command = "${python} touch.py ${out}"
}
build "out2.txt" : touch
alias all : "out2.txt"
''',
    }, runs=[
        {"name": "regen_1", 
         "before": [{"sleep": 1.2}, {"touch": ["gen.in"]}],
         "expect": {"exit": 0, "files": ["out2.txt"], "filesNot": ["out1.txt"],
                    "stderrHas": ["Build manifest was regenerated"]}},
        {"name": "noop", 
         "before": [{"sleep": 1.2}], 
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "regen_2", 
         "before": [
            {"sleep": 1.2}, 
            {"modify": {"gen.in": pp('''pmake_required_version = 1.0
python = "@PYTHON@"
rule regen {
    command = "${python} copy.py gen.in build.pmake"
    generator = true
}
build "build.pmake" : regen "gen.in"

rule touch {
    command = "${python} touch.py ${out}"
}
build "out3.txt" : touch
alias all : "out3.txt"
''')}}
        ], "expect": {"exit": 0, "files": ["out3.txt"], 
                      "stderrHas": ["Build manifest was regenerated"]}},
    ], description="PMake-style manifest regeneration (3 scenarios)"),
]