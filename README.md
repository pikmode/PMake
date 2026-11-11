# PMake

> Fast cross-platform build system with a scripting DSL and native Ninja compatibility.

**One file. One command. Just build.**

PMake combines the speed of Ninja with the expressiveness of a scripting language.
Write a single `build.pmake` — no code generation, no second tool in the chain.

---

## Why PMake?

- **One file, one command.** No `cmake . && ninja`. Just `pmake`.
- **Fast.** Rebuild speed on par with Ninja; minimal startup overhead.
- **Expressive DSL.** `if` / `foreach` / `function` / `include` — without an external generator.
- **Smart configurations.** Native `--config debug/release` and `--platform` support with automatic cache invalidation.
- **Ninja-compatible.** Parses `.ninja` files directly (drop-in for CMake/Meson output).
- **Incremental by default.** Binary cache, depfile, restat, generator.
- **Cross-platform.** Windows, Linux, macOS.
- **No dependencies.** Single static binary. No Python, no Qt, no runtime.

---

## Quick start

### Build from source

**Windows (MinGW):**

```bat
g++ -std=c++23 -O2 -DNDEBUG -static -static-libgcc -static-libstdc++ -I src ^
    src\main.cpp src\core\*.cpp src\parser\*.cpp src\parser\ninja-parser\*.cpp ^
    src\executor\*.cpp src\deps\*.cpp src\util\*.cpp src\ui\*.cpp ^
    -o pmake.exe ^
    -lmpr -luserenv -lkernel32 -luser32 -lgdi32 -lwinspool ^
    -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -lstdc++exp
```

**Linux / macOS:**

```bash
g++ -std=c++23 -O2 -DNDEBUG -I src $(find src -name '*.cpp' | sort) -o pmake -pthread
```

### Hello world

**`build.pmake`:**

```pmake
pmake_required_version = 1.0

rule echo {
    command     = "echo ${msg} > ${out}"
    description = "echo ${out}"
}

build "hello.txt" : echo { msg = "Hello, PMake!" }
alias all : "hello.txt"
```

Run:

```bash
pmake
cat hello.txt   # Hello, PMake!
```

---

## Real-world example

Compiling a C++ project with depfile tracking:

```pmake
pmake_required_version = 1.0

cxx   = "g++"
flags = "-std=c++20 -O2 -Wall"

rule cc {
    command     = "${cxx} ${flags} -c ${in} -o ${out} -MMD -MF ${out}.d"
    description = "compiling ${in}"
    depfile     = "${out}.d"
    deps        = "gcc"
}

rule link {
    command     = "${cxx} ${flags} ${in} -o ${out}"
    description = "linking ${out}"
}

sources = glob("*.cpp", "src")
objects = replace_suffix(sources, ".o")

build ${objects} : cc ${sources}
build "app" : link ${objects}
alias all : "app"
```

Run:

```bash
pmake           # build
pmake -j 8      # parallel
pmake -n        # dry-run
pmake clean     # clean outputs
```

---

## Syntax at a glance

| Construct           | Example |
|-----------          |---------|
| Variable            | `flags = "-O2"` |
| Append              | `flags += "-Wall"` |
| Interpolation       | `"${flags} -c ${in}"` |
| Rule                | `rule cc { command = "..." }` |
| Build               | `build "out.o" : cc "in.cpp"` |
| Multiple outputs    | `build "a.txt", "b.txt" : gen` |
| Order-only          | `build "out" : link "in" \|\| "stamp"` |
| Implicit            | `build "out" : link "in" \| "hdr"` |
| Alias               | `alias all : "app"` |
| Include             | `include "rules.pmake"` |
| If                  | `if os == "windows" { ... }` |
| Foreach             | `foreach f in ${sources} { ... }` |
| Function            | `function add_flag(x) { flags += x }` |

---

## Command-line

```
pmake [options] [command] [target]

Options:
  -V, --version           show version
  -f, --file <file>       build file (default: build.pmake, then build.ninja)
  -j, --jobs <N>          parallel jobs
  --clean                 remove outputs
  --config <name>         build configuration (debug/release)
  --platform <name>       target platform
  -v, --verbose           verbose output (per-task description/command/workdir)
  -q, --quiet             minimal output
  -n, --dry-run           show what would be built
  --no-color              disable colors
  -h, --help              help
```

---

## Building and testing

```bash
# Build
g++ -std=c++23 -O2 $(find src -name '*.cpp') -o pmake -pthread

# Run the test suite (Windows)
cd tests-pmake-1.0/windows
python run.py --generate-only
python run.py --pmake ../../build/pmake/PMake.exe

# Run the test suite (Linux/macOS)
cd tests-pmake-1.0/linux
python run.py --generate-only
python run.py --pmake ../../build/linux/pmake
```

Expected: **111 / 111 passed, 0 regressions**.

---

## Documentation

Full language reference (Russian): [`readme.txt`](readme.txt)

Covers: variables, rules, builds, aliases, includes, `if`/`foreach`/`while`/`switch`,
functions, builtins, depfile, restat, rspfile, caching, Windows specifics.

---

## Project status

- **Version:** 1.2.0
- **Tests:** 111 / 111 passing
- **Maturity:** stable for small and medium projects
- **Roadmap:** thread pool, `--export-compile-commands`, stdlib of common rules

---

## License

Licensed under the **Apache License 2.0** — see [`LICENSE`](LICENSE).

---

## About

Developed by **PikMode**.

Contributions, issues, and feature requests are welcome.