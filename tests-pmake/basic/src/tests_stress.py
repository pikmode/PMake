# -*- coding: utf-8 -*-
"""
Stress-тесты для v1.3.0 (Performance & Scale).

Покрывают:
  * DepsLog StringPool + prefetch (200 depfile-нод × общий hdr.h)
  * Thread pool (wide fanout, deep chain, parallel 50, ninja 200-edge)
  * Include depth limits (50 уровней)
  * Foreach 200 итераций
  * Rspfile с 500 inputs
  * Ctrl+C + resume на 30 задачах
  * Depfile с 100 headers (DepsLog lookup)
  * Multi-output 50
  * Restat cascade 20
  * Order-only 50
  * Alias chain 30
  * Mixed graph (chain + fanout)
"""
from ._common import test, pp


# ============================================================
# Generator-функции
# ============================================================

def _stress_depfile(n=200):
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule cc {',
        '    command = "${python} cc_make_dep.py ${in} ${out} hdr.h"',
        '    depfile = "${out}.d"',
        '    deps = "gcc"',
        '}',
    ]
    for i in range(1, n + 1):
        lines.append(f'build "obj/f_{i:04d}.o" : cc "src/f_{i:04d}.c"')
    lines.append('alias all : ' + ", ".join(f'"obj/f_{i:04d}.o"' for i in range(1, n + 1)))

    files = {"build.pmake": "\n".join(lines), "hdr.h": "// header\n"}
    for i in range(1, n + 1):
        files[f"src/f_{i:04d}.c"] = f"int x_{i};\n"
    return files


def _stress_deep_chain(depth=50):
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule copy { command = "${python} copy.py ${in} ${out}" }',
        'build "c0.out" : copy "src.in"',
    ]
    for i in range(1, depth + 1):
        lines.append(f'build "c{i}.out" : copy "c{i-1}.out"')
    lines.append(f'alias all : "c{depth}.out"')
    return {"build.pmake": "\n".join(lines), "src.in": "chain\n"}


def _stress_wide_fanout(n=100):
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule copy { command = "${python} copy.py ${in} ${out}" }',
    ]
    for i in range(1, n + 1):
        lines.append(f'build "f_{i:04d}.out" : copy "src.in"')
    lines.append('alias all : ' + ", ".join(f'"f_{i:04d}.out"' for i in range(1, n + 1)))
    return {"build.pmake": "\n".join(lines), "src.in": "fanout\n"}


def _stress_include(n=50):
    files = {
        "build.pmake": 'pmake_required_version = 1.3\ninclude "f0.pmake"\nalias all :\n'
    }
    for i in range(n):
        files[f"f{i}.pmake"] = f'include "f{i + 1}.pmake"\n'
    files[f"f{n}.pmake"] = 'pmake_required_version = 1.3\n'
    return files


def _stress_foreach(n=200):
    items = " ".join(f"t{i}" for i in range(n))
    out_list = ", ".join(f'"out_t{i}.txt"' for i in range(n))
    return {"build.pmake": f'''pmake_required_version = 1.3
python = "@PYTHON@"
rule touch {{ command = "${{python}} touch.py ${{out}}" }}
items = "{items}"
foreach x in ${{items}} {{
    build "out_${{x}}.txt" : touch
}}
alias all : {out_list}
'''}


def _stress_big_rspfile(n=500):
    files = {f"in_{i:04d}.txt": f"data_{i}\n" for i in range(1, n + 1)}
    inputs = " ".join(f'"in_{i:04d}.txt"' for i in range(1, n + 1))
    files["build.pmake"] = f'''pmake_required_version = 1.3
python = "@PYTHON@"
rule link {{
    command = "${{python}} cat_rspfile.py ${{rspfile}} ${{out}}"
    rspfile = "${{out}}.rsp"
    rspfile_content = "${{in}}"
}}
build "out.txt" : link {inputs}
alias all : "out.txt"
'''
    return files


def _stress_interrupt(n=30):
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule slow { command = "${python} delayed_echo.py 300 ${out} x" }',
    ]
    for i in range(1, n + 1):
        lines.append(f'build "k_{i:03d}.txt" : slow')
    lines.append('alias all : ' + ", ".join(f'"k_{i:03d}.txt"' for i in range(1, n + 1)))
    return {"build.pmake": "\n".join(lines)}


def _stress_ninja(n=200):
    lines = [
        'rule touch',
        '  command = "@PYTHON@" touch.py $out',
        '',
    ]
    for i in range(n):
        lines.append(f'build out_{i:04d}.txt : touch')
    lines.append('')
    lines.append('build all : phony ' + ' '.join(f'out_{i:04d}.txt' for i in range(n)))
    lines.append('default all')
    return {"build.ninja": "\n".join(lines)}


def _stress_many_headers(n_hdrs=100):
    hdrs = [f"include/hdr_{i:03d}.h" for i in range(n_hdrs)]
    depfile_content = f"test.o: src.c {' '.join(hdrs)}\n"

    files = {
        "build.pmake": '''pmake_required_version = 1.3
python = "@PYTHON@"
rule cc {
    command = "${python} compile_with_deps.py ${out}"
    depfile = "${out}.d"
    deps = "gcc"
}
build "test.o" : cc "src.c"
alias all : "test.o"
''',
        "src.c": "int x;\n",
        "test.o.d": depfile_content,
        "compile_with_deps.py": '''import pathlib
import sys

out = pathlib.Path(sys.argv[1])
out.touch()
headers = sorted(pathlib.Path("include").glob("*.h"))
dependencies = " ".join(path.as_posix() for path in headers)
pathlib.Path(str(out) + ".d").write_text(
    f"{out}: src.c {dependencies}\\n", encoding="utf-8"
)
''',
    }
    for h in hdrs:
        files[h] = "// header\n"
    return files


def _stress_msvc_dependencies(n=64):
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule cc {',
        '    command = "${python} fake_msvc.py ${in} ${out}"',
        '    deps = "msvc"',
        '}',
    ]
    files = {
        "build.pmake": "",
        "include/shared header.h": "// shared header\n",
        "unrelated.h": "// unrelated\n",
        "fake_msvc.py": '''import pathlib
import sys

source, output = map(pathlib.Path, sys.argv[1:3])
index = source.stem.split("_")[-1]
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(index, encoding="utf-8")
print("Note: including file: include/shared header.h")
print(f"Note: including file: include/per file/hdr_{index}.h")
''',
    }
    outputs = []
    for i in range(n):
        source = f"src/unit_{i:03d}.c"
        output = f"obj/unit_{i:03d}.obj"
        header = f"include/per file/hdr_{i:03d}.h"
        lines.append(f'build "{output}" : cc "{source}"')
        files[source] = f"// unit {i}\n"
        files[header] = f"// header {i}\n"
        outputs.append(f'"{output}"')
    lines.append("alias all : " + ", ".join(outputs))
    files["build.pmake"] = "\n".join(lines)
    return files


def _stress_multi_output(n=50):
    outputs = ", ".join(f'"o_{i:03d}.txt"' for i in range(n))
    return {"build.pmake": f'''pmake_required_version = 1.3
python = "@PYTHON@"
rule gen {{ command = "${{python}} multi_out.py ${{out}}" }}
build {outputs} : gen
alias all : {outputs}
'''}


def _stress_restat_cascade(n=20):
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule cp { command = "${python} copy_preserve.py ${in} ${out}" restat = true }',
        'build "c0.out" : cp "src.in"',
    ]
    for i in range(1, n + 1):
        lines.append(f'build "c{i}.out" : cp "c{i-1}.out"')
    lines.append(f'alias all : "c{n}.out"')
    return {"build.pmake": "\n".join(lines), "src.in": "constant\n"}


def _stress_parallel(n=50):
    """50 независимых задач, -j8 — thread pool contention."""
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule sleep_touch { command = "${python} sleep_and_touch.py 50 ${out}" }',
    ]
    for i in range(1, n + 1):
        lines.append(f'build "p_{i:03d}.txt" : sleep_touch')
    lines.append('alias all : ' + ", ".join(f'"p_{i:03d}.txt"' for i in range(1, n + 1)))
    return {"build.pmake": "\n".join(lines)}


def _stress_order_only(n=50):
    """50 outputs с order-only на общий stamp."""
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule touch { command = "${python} touch.py ${out}" }',
        'build "stamp.txt" : touch',
    ]
    for i in range(1, n + 1):
        lines.append(f'build "o_{i:03d}.txt" : touch || "stamp.txt"')
    lines.append('alias all : "stamp.txt", ' +
                 ", ".join(f'"o_{i:03d}.txt"' for i in range(1, n + 1)))
    return {"build.pmake": "\n".join(lines)}


def _stress_alias_chain(n=30):
    """30 phony alias в цепочке: a_01 → a_02 → ... → a_30 → base.txt."""
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule touch { command = "${python} touch.py ${out}" }',
        'build "base.txt" : touch',
    ]
    for i in range(1, n + 1):
        prev = 'base.txt' if i == 1 else f'a_{i-1:02d}'
        lines.append(f'build "a_{i:02d}" : phony "{prev}"')
    lines.append(f'alias all : "a_{n:02d}"')
    return {"build.pmake": "\n".join(lines)}


def _stress_mixed(n_chain=15, n_fanout=30):
    """Смешанный граф: c0..c{n_chain} (n_chain+1 нод) + fanout n_fanout.
    Touch src → (n_chain+1) + n_fanout задач.
    Для n_chain=15, n_fanout=30: 16 + 30 = 46."""
    lines = [
        'pmake_required_version = 1.3',
        'python = "@PYTHON@"',
        'rule copy { command = "${python} copy.py ${in} ${out}" }',
        'build "c0.out" : copy "src.in"',
    ]
    for i in range(1, n_chain + 1):
        lines.append(f'build "c{i}.out" : copy "c{i-1}.out"')
    for i in range(1, n_fanout + 1):
        lines.append(f'build "f_{i:03d}.out" : copy "c{n_chain}.out"')
    lines.append('alias all : ' + ", ".join(f'"f_{i:03d}.out"' for i in range(1, n_fanout + 1)))
    return {"build.pmake": "\n".join(lines), "src.in": "mixed\n"}


# ============================================================
# Готовые files-словари
# ============================================================

_stress_dep_files      = _stress_depfile(200)
_stress_headers_files  = _stress_many_headers(100)
_stress_msvc_files     = _stress_msvc_dependencies(64)


TESTS = [
    # 1. DepsLog StringPool + prefetch
    test("stress_depfile_200", _stress_dep_files, runs=[
        {"name": "first_build", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 200}},
        {"name": "noop", "before": [{"sleep": 0.5}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_hdr_all_rebuild",
         "before": [{"sleep": 0.3}, {"touch": ["hdr.h"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 200}},
    ], description="200 depfile-нод × hdr.h: noop=0, touch hdr → 200",
       timeout=180),

    # 2. Deep chain
    test("stress_deep_chain_50", _stress_deep_chain(50), runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 51}},
        {"name": "noop", "before": [{"sleep": 0.5}],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_src_rebuild_chain",
         "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "expect": {"exit": 0, "taskCount": 51}},
    ], description="50-уровневая цепочка: touch src → 51 задача", timeout=120),

    # 3. Wide fanout
    test("stress_wide_fanout_100", _stress_wide_fanout(100), runs=[
        {"name": "first", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 100}},
        {"name": "touch_src_rebuild_all",
         "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 100}},
    ], description="1 source → 100 outputs, touch → 100 задач", timeout=120),

    # 4. Include 50
    test("stress_include_50", _stress_include(50), runs=[
        {"name": "deep_50_ok", "expect": {"exit": 0}},
    ], description="50-уровневый include chain (< MAX=200)", timeout=60),

    # 5. Foreach 200
    test("stress_foreach_200", _stress_foreach(200), runs=[
        {"name": "first", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 200}},
    ], description="foreach 200 → 200 build-нод", timeout=120),

    # 6. Rspfile 500
    test("stress_big_rspfile_500", _stress_big_rspfile(500), runs=[
        {"name": "build_with_big_rsp",
         "expect": {"exit": 0, "taskCount": 1, "files": ["out.txt"]}},
    ], description="rspfile с 500 inputs", timeout=60),

    # 7. Ctrl+C + resume
    test("stress_interrupt_30", _stress_interrupt(30), runs=[
        {"name": "kill_mid_build", "args": ["-f", "build.pmake", "-j", "4"],
         "kill_after_ms": 700, "continue": True, "expect": {}},
        {"name": "resume_completes",
         "before": [{"sleep": 0.5}],
         "args": ["-f", "build.pmake", "-j", "4"],
         "expect": {"exit": 0,
                    "files": [f"k_{i:03d}.txt" for i in range(1, 31)]}},
    ], description="Ctrl+C mid-30 + resume", timeout=120),

    # 8. Ninja 200
    test("stress_ninja_200", _stress_ninja(200), runs=[
        {"name": "first_build",
         "args": ["-f", "build.ninja", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 200}},
        {"name": "noop", "before": [{"sleep": 0.5}],
         "args": ["-f", "build.ninja", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="200-edge build.ninja: parse + noop", timeout=180),

    # 9. Depfile 100 headers
    test("stress_many_headers_100", _stress_headers_files, runs=[
        {"name": "first_build",
         "expect": {"exit": 0, "taskCount": 1, "files": ["test.o"]}},
        {"name": "noop", "before": [{"sleep": 0.5}],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "touch_header_50_rebuild",
         "before": [{"sleep": 0.3}, {"touch": ["include/hdr_050.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "touch_header_99_rebuild",
         "before": [{"sleep": 0.3}, {"touch": ["include/hdr_099.h"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="depfile с 100 headers: noop=0, touch 1 hdr → 1 rebuild",
       timeout=60),

    # 9a. MSVC dynamic dependencies under parallel load.
    test("stress_msvc_dependencies_64", _stress_msvc_files, runs=[
        {"name": "parallel_first_build",
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 64}},
        {"name": "cached_graph_is_noop",
         "before": [{"sleep": 0.4}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "shared_header_invalidates_every_object",
         "before": [{"sleep": 0.3}, {"touch": ["include/shared header.h"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 64}},
        {"name": "single_header_invalidates_one_object",
         "before": [{"sleep": 0.3}, {"touch": ["include/per file/hdr_037.h"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "untracked_header_does_not_invalidate",
         "before": [{"sleep": 0.3}, {"touch": ["unrelated.h"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="64 parallel MSVC-style dep scans preserve shared and per-output edges",
       timeout=120),

    # 10. Multi-output 50
    test("stress_multi_output_50", _stress_multi_output(50), runs=[
        {"name": "first_build",
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "noop", "before": [{"sleep": 0.5}],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_one_rebuild_all",
         "before": [{"sleep": 0.3}, {"delete": ["o_025.txt"]}],
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="multi-output 50: 1 task, delete 1 → 1 task", timeout=60),

    # 11. Restat cascade
    test("stress_restat_cascade_20", _stress_restat_cascade(20), runs=[
        {"name": "first_build", "args": ["-f", "build.pmake", "-j", "4"],
         "expect": {"exit": 0, "taskCount": 21}},
        {"name": "touch_src_only_first",
         "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "args": ["-f", "build.pmake", "-j", "4"],
         "expect": {"exit": 0, "taskCount": 1}},
        {"name": "modify_src_cascades",
         "before": [{"sleep": 0.3}, {"modify": {"src.in": "changed\n"}}],
         "args": ["-f", "build.pmake", "-j", "4"],
         "expect": {"exit": 0, "taskCount": 21}},
    ], description="restat cascade 20: touch=1, modify=21", timeout=120),

    # ============================================================
    # 12. parallel 50 -j8 — thread pool contention.
    # ============================================================
    test("stress_parallel_50", _stress_parallel(50), runs=[
        {"name": "j8_build",
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 50}},
    ], description="50 parallel tasks -j8 (thread pool)", timeout=120),

    # ============================================================
    # 13. order-only 50.
    #
    # CHANGED: touch stamp → delete stamp.
    # `build "stamp.txt" : touch` — нет inputs. `touch stamp.txt`
    # обновляет mtime output, но isDirty = false (output существует,
    # deps пусто). Это правильная make/ninja семантика — touch output
    # без inputs не делает его dirty.
    # Delete stamp → anyExists=false → isDirty=true → rebuild.
    # order-only: o_* не зависят по mtime, остаются up-to-date.
    # ============================================================
    test("stress_order_only_50", _stress_order_only(50), runs=[
        {"name": "first", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 51}},
        {"name": "noop", "before": [{"sleep": 0.5}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 0}},
        {"name": "delete_stamp_only_stamp",
         "before": [{"sleep": 0.3}, {"delete": ["stamp.txt"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         # stamp missing → rebuild. o_* уже собраны, order-only
         # не propagate по mtime → skip. taskCount=1.
         "expect": {"exit": 0, "taskCount": 1}},
    ], description="order-only 50: delete stamp → только stamp (tasks=1)",
       timeout=120),

    # ============================================================
    # 14. Alias chain 30.
    # ============================================================
    test("stress_alias_chain_30", _stress_alias_chain(30), runs=[
        {"name": "resolve_30_aliases",
         "expect": {"exit": 0, "taskCount": 1, "files": ["base.txt"]}},
        {"name": "noop", "before": [{"sleep": 0.3}],
         "expect": {"exit": 0, "taskCount": 0}},
    ], description="30 phony alias в цепочке → 1 реальная задача", timeout=60),

    # ============================================================
    # 15. Mixed graph — chain 15 + fanout 30.
    #
    # CHANGED: taskCount 45 → 46.
    # Считали 15 chain + 30 fanout = 45, но c0.out : copy "src.in" —
    # это тоже build-нода. Цепочка c0..c15 = 16 нод (n_chain=15 →
    # 15 build + c0 = 16). Итого 16 + 30 = 46. pmake прав, off-by-one
    # в ожидании.
    # ============================================================
    test("stress_mixed_graph_30", _stress_mixed(15, 30), runs=[
        {"name": "first", "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 46}},
        {"name": "touch_src_rebuild_all",
         "before": [{"sleep": 0.3}, {"touch": ["src.in"]}],
         "args": ["-f", "build.pmake", "-j", "8"],
         "expect": {"exit": 0, "taskCount": 46}},
    ], description="mixed chain 15 + fanout 30: 46 задач (c0..c15 + fanout)",
       timeout=120),
]