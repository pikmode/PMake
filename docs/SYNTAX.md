# PMake DSL — Syntax Reference

## Variables

```pmake
foo    = "bar"          // string
count  = 42             // number
flag   = true           // bool
list   = [1, 2, 3]      // list
foo    += " baz"        // append (space-separated)
```

Interpolation: `${foo}` inside string literals. `$foo` also works for
simple identifiers (alnum + `_`).

## Rules

```pmake
rule cc {
    command     = "${CXX} ${FLAGS} -c ${in} -o ${out}"
    description = "compiling ${in}"
    depfile     = "${out}.d"
    deps        = "gcc"
    restat      = true
    generator   = false
    pool        = "console"
    rspfile     = "${out}.rsp"
    rspfile_content = "${in}"
}
```

| Field | Meaning |
|---|---|
| `command` | Program and arguments to run. Shell syntax is not interpreted. |
| `description` | Human-readable line (default: output path) |
| `depfile` | Path to Makefile-format `.d` file |
| `deps` | `"gcc"` for depfile parsing |
| `restat` | Skip downstream rebuild if output mtime unchanged |
| `generator` | Auto-regen on start (like CMake's RERUN) |
| `pool` | `"console"` for serialized, or custom pool name |
| `rspfile` | Response file path (written before command) |
| `rspfile_content` | Content of response file |

`${in}`, `${out}`, `${in_newline}`, and `${rspfile}` are available in
`command`, `description`, and `rspfile_content`.

Shell operators such as `>`, `|`, and `&&` are not interpreted by PMake. To use
shell features, explicitly invoke a shell (for example, `cmd /c` on Windows or
`sh -c` on Linux/macOS).

## Builds

```pmake
build "out.o" : cc "in.cpp"                          // simple
build "a", "b" : gen                                 // multi-output
build "out" : link "a.o" "b.o"                       // multiple inputs
build "out" : link "in" | "header"                   // implicit (rebuild on header change)
build "out" : link "in" || "stamp"                   // order-only (wait, don't rebuild)
build "out" : link "in" { flags = "-O3" }            // per-build vars
```

## Aliases

```pmake
alias all : "app", "tests"
```

## Includes

```pmake
include "rules.pmake"    // sub-scope, sees parent's vars
import "vars.pmake"      // no version check, no sub-scope
```

Both detect cycles and support relative paths.

## Control flow

```pmake
if config == "debug" {
    flags = "-g"
} elseif config == "release" {
    flags = "-O3"
} else {
    flags = "-O2"
}

foreach src in ${sources} {
    build "obj/${basename(src)}.o" : cc ${src}
}

while i < 10 {
    // ...
}

switch mode {
    case "debug" { flags = "-g" }
    case "release" { flags = "-O3" }
    default { flags = "-O2" }
}
```

`break` / `continue` inside `foreach` / `while`.

## Functions

```pmake
function add_flag(flag) {
    flags += ${flag}
    return "done"
}

result = add_flag("-Wall")
```

Parameters are scoped to the call. Functions can return values.

## Configurations & platforms

```pmake
config debug {
    flags = "-g"
}
config release {
    flags = "-O3"
}

platform windows {
    exe_ext = ".exe"
}
platform linux {
    exe_ext = ""
}
```

Applied automatically based on `--config` / `--platform`. Change → cache
invalidated.

## Namespaces

```pmake
namespace mylib {
    // vars scoped to namespace
    internal = "value"
}
```

## Constants & enums

```pmake
const BUFFER_SIZE = 4096
enum Color { RED, GREEN, BLUE }
```

## Builtins

| Function | Returns |
|---|---|
| `glob(pattern, dir?)` | list of files |
| `basename(path)` | filename |
| `dirname(path)` | parent dir (absolute) |
| `extension(path)` | without dot |
| `filename(path)` | filename with ext |
| `join(items..., sep)` | joined string |
| `replace(s, from, to)` | replaced string |
| `replace_suffix(list, new_ext)` | list with new extensions |
| `split(s, delim)` | list |
| `starts_with(s, prefix)` / `ends_with` / `contains` | bool |
| `env(name)` | env var or empty |
| `exists(path)` | bool |
| `file_mtime(path)` | int64 mtime |
| `read_file(path)` | contents |
| `write_file(path, content)` | bool |
| `hash(s)` | sha1 hex |
| `cwd()` | current dir |
| `absolute_path(p)` | absolute |
| `relative_path(a, b)` | relative from b to a |
| `dep_inputs(path)` | parse .d file → inputs |

## Special variables (per rule)

Inside `command`, `description`, `rspfile_content`:

- `${in}` — space-joined quoted inputs
- `${out}` — space-joined quoted outputs
- `${in_newline}` — inputs separated by newline (for rspfiles)
- `${rspfile}` — path to response file (if rule has rspfile)

## Comments

```pmake
// line comment
/* block
   comment */
```

**Not** `#` — that's a parse error with an actionable hint.
