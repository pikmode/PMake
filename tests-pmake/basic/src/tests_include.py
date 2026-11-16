# -*- coding: utf-8 -*-
from ._common import test

TESTS = [
    test("include_basic", {
        "build.pmake": '''pmake_required_version = 1.0
include "sub.pmake"
build "out.txt" : echo
alias all : "out.txt"
''',
        "sub.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} from_sub" }
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "from_sub"}}},
    ], description="include простого файла"),

    test("include_relative", {
        "build.pmake": '''pmake_required_version = 1.0
include "subdir/part.pmake"
build "out.txt" : echo
alias all : "out.txt"
''',
        "subdir/part.pmake": 'pmake_required_version = 1.0\ninclude "nested.pmake"\n',
        "subdir/nested.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} echo_to.py ${out} nested_ok" }
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "nested_ok"}}},
    ], description="include с относительным путём"),

    test("include_cycle", {
        "build.pmake": 'pmake_required_version = 1.0\ninclude "a.pmake"\n',
        "a.pmake": 'pmake_required_version = 1.0\ninclude "b.pmake"\n',
        "b.pmake": 'pmake_required_version = 1.0\ninclude "a.pmake"\n',
    }, runs=[
        {"name": "cycle_detected", "expect": {"exit": 1, "stderrHas": ["ycl"]}},
    ], description="include cycle → ошибка, не крашить"),

    test("real_include_parent_vars_in_build", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
OUT_DIR = "out_dir"
include "sub.pmake"
alias all : "${OUT_DIR}/result.txt"
''',
        "sub.pmake": '''pmake_required_version = 1.0
rule echo { command = "${python} echo_to.py ${out} hello" }
build "${OUT_DIR}/result.txt" : echo
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "files": ["out_dir/result.txt"],
                                     "filesWith": {"out_dir/result.txt": "hello"}}},
    ], description="include видит переменные родителя в build output"),

    test("real_include_skip_by_if_false", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
mode = "release"
if mode == "debug" {
    include "debug_only.pmake"
}
rule echo { command = "${python} echo_to.py ${out} ok" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 1,
                                     "filesWith": {"out.txt": "ok"}}},
    ], description="include внутри if false не выполняется"),

    # ============================================================
    # NEW (Issue B + Issue C): глубокие include-цепочки + stack-safe лимиты.
    #
    # Issue B: конструктор ParserImpl сбрасывал includeDepth в 0 →
    #   лимит не работал (каждый sub видел depth=1).
    # Issue C: MAX_INCLUDE_DEPTH=5000 → stack overflow.
    #   Снижен до 200 (~200 KB стека).
    #
    # Также: error в depth-check был recover=true → silently
    #   игнорировался. Теперь recover=false → throw → abort.
    #
    # ВАЖНО: build.pmake ОБЯЗАТЕЛЬНО содержит "alias all :" —
    #   иначе engine.run("all") упадёт с "target not found" (exit 1)
    #   ДО того, как мы увидим результат include-обработки.
    #   Раньше тесты падали именно из-за этого (exit 1, пустой stderr).
    # ============================================================
    test("include_deep_50_works", {
        # build.pmake → f0 → ... → f48 → f49 (stub).
        # 50 include-уровней. Лимит 200 — не превышен.
        "build.pmake": '''pmake_required_version = 1.0
include "f0.pmake"
alias all :
''',
        **{f"f{i}.pmake": f'include "f{i+1}.pmake"\n' for i in range(49)},
        "f49.pmake": 'pmake_required_version = 1.0\n',
    }, runs=[
        {"name": "deep_50_ok", "expect": {"exit": 0}},
    ], description="50-deep include chain (< MAX_INCLUDE_DEPTH=200)",
       timeout=30),

    test("include_depth_limit_201", {
        # build.pmake → f0 → ... → f199 → f200.
        # 201 include-уровней > 200 → на 201-м depth-check бросает
        # "Include depth exceeds 200". Abort до чтения f200.
        "build.pmake": '''pmake_required_version = 1.0
include "f0.pmake"
alias all :
''',
        **{f"f{i}.pmake": f'include "f{i+1}.pmake"\n' for i in range(200)},
        "f200.pmake": 'pmake_required_version = 1.0\n',
    }, runs=[
        {"name": "hits_depth_limit", "expect": {
            "exit": 1,
            "stderrHas": ["Include depth exceeds 200"],
        }},
    ], description="201-deep chain → MAX_INCLUDE_DEPTH=200 enforced",
       timeout=30),
]