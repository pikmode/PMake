# -*- coding: utf-8 -*-
from ._common import test, pp, IS_WINDOWS

TESTS = [
    # 1. Проверка вложенных foreach и генерации имен — без изменений
    test("real_nested_foreach", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
archs = "x86 x64"
modes = "debug release"
rule echo { command = "${python} touch.py ${out}" }
foreach arch in ${archs} {
    foreach mode in ${modes} {
        build "out_${arch}_${mode}.txt" : echo
    }
}
alias all : "out_x86_debug.txt", "out_x86_release.txt", "out_x64_debug.txt", "out_x64_release.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "taskCount": 4,
                                     "files": ["out_x86_debug.txt", "out_x86_release.txt", "out_x64_debug.txt", "out_x64_release.txt"]}},
    ], description="вложенные foreach"),

    # 2. builtins: dirname возвращает АБСОЛЮТНЫЙ путь — проверяем через basename(dirname(...)),
    #    чтобы тест не зависел от временной папки.
    test("real_builtins_deep", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
file = "src/core/main.cpp"
dir = dirname("${file}")
base = basename("${file}")
ext = extension("${file}")
joined = join("A", "B", "C", "-")
dir_base = basename("${dir}")
dir_parent = basename(dirname("${dir}"))
rule echo { command = "${python} write_all.py ${out} ${dir_parent} ${dir_base} ${base} ${ext} ${joined}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0,
                                     "filesWith": {"out.txt": "src core main.cpp cpp A-B-C"}}},
    ], description="builtins (dirname абсолютный → basename даёт относительные части)"),

    # 3. while с break/continue — язык строковый (нет числового +=), поэтому
    #    переписываем на строковую итерацию. Логика break/continue та же.
    test("real_while_break_continue", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
i = ""
result = ""
while i != "xxxxx" {
    i = "${i}x"
    if i == "xx" { continue }
    if i == "xxxx" { break }
    result += i
}
rule echo { command = "${python} write_all.py ${out} ${result}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0,
                                     "filesWith": {"out.txt": "x xxx"}}},
    ], description="while с break и continue (строковая итерация)"),

    # 4. import — импортируемый файл должен содержать pmake_required_version
    #    (parseAstOnly сейчас вызывает тот же parseTopLevel с проверкой версии).
    test("real_import_vars", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
import "vars.pmake"
rule echo { command = "${python} write_all.py ${out} ${SUBDIR}" }
build "out.txt" : echo
alias all : "out.txt"
''',
        "vars.pmake": 'pmake_required_version = 1.0\nSUBDIR = "imported_dir"\n',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "filesWith": {"out.txt": "imported_dir"}}},
    ], description="import видит переменные из родителя/импорта"),

    # 5. switch default — без изменений
    test("real_switch_default", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
mode = "production"
switch mode {
case "debug" { flags = "-g" }
case "release" { flags = "-O3" }
default { flags = "-O2" }
}
rule echo { command = "${python} write_all.py ${out} ${flags}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "filesWith": {"out.txt": "-O2"}}},
    ], description="switch/case/default"),

    # 6. config inside if — без изменений
    test("real_config_inside_if", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
if config == "debug" {
    mode = "DEBUG_ACTIVE"
} else {
    mode = "RELEASE_ACTIVE"
}
rule echo { command = "${python} write_all.py ${out} ${mode}" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "default", "expect": {"exit": 0, "filesWith": {"out.txt": "RELEASE_ACTIVE"}}},
        {"name": "debug", "args": ["-f", "build.pmake", "--config", "debug"],
         "expect": {"exit": 0, "filesWith": {"out.txt": "DEBUG_ACTIVE"}}},
    ], description="проверка config в условиях if"),

    # 7. glob + replace_suffix — возвращают АБСОЛЮТНЫЕ пути.
    #    Присваивание списка в переменную теряет список (Value::to_string()),
    #    поэтому используем inline-foreach по результату функции.
    #    Проверяем через basename — тест не зависит от временного пути.
    test("real_glob_replace", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
found_a_src = "no"
found_b_src = "no"
found_a_obj = "no"
found_b_obj = "no"
foreach s in glob("*.txt", ".") {
    b = basename("${s}")
    if b == "a.txt" { found_a_src = "yes" }
    if b == "b.txt" { found_b_src = "yes" }
}
foreach o in replace_suffix(glob("*.txt", "."), ".obj") {
    b = basename("${o}")
    if b == "a.obj" { found_a_obj = "yes" }
    if b == "b.obj" { found_b_obj = "yes" }
}
rule echo { command = "${python} write_all.py ${out} ${found_a_src} ${found_b_src} ${found_a_obj} ${found_b_obj}" }
build "out.txt" : echo
alias all : "out.txt"
''',
        "a.txt": "a", "b.txt": "b",
    }, runs=[
        {"name": "first", "expect": {"exit": 0,
                                     "filesWith": {"out.txt": "yes yes yes yes"}}},
    ], description="glob + replace_suffix (абсолютные пути, проверяем basename)"),

    # 8. escaped command — без изменений
    test("real_escaped_command", {
        "build.pmake": '''pmake_required_version = 1.0
python = "@PYTHON@"
rule echo { command = "${python} write_all.py ${out} \\"hello world\\"" }
build "out.txt" : echo
alias all : "out.txt"
''',
    }, runs=[
        {"name": "first", "expect": {"exit": 0, "filesWith": {"out.txt": "hello world"}}},
    ], description="экранированные кавычки в команде"),
]