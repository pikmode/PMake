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
- **Ninja manifest support.** Reads compatible `build.ninja` files directly;
  see the compatibility guide for supported features and limitations.
- **Incremental by default.** Binary cache, depfile, restat, generator.
- **Cross-platform.** Windows, Linux, macOS.
- **Simple deployment.** A single executable; the release build is statically
  linked by default when supported by the toolchain. No Python or Qt runtime
  is required.
- **Scales to monorepos.** Thread pool + depfile prefetch + StringPool DepsLog — designed for 100K+ edges.
- **Safe `clean`.** `pmake -t clean` never touches source files (`.cpp`, `.cmake`, `CMakeLists.txt`).

---

## Quick start

### Build from source

**Recommended: CMake + Ninja** (CMake 3.25+ and a C++23 compiler).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# → build/pmake (Linux/macOS), build/pmake.exe (Windows)
```

To rebuild PMake with PMake, use the generated Ninja manifest:

```bash
cd build
pmake
```

### Build a Hello World program

Create `main.cpp`:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello, PMake!\n";
}
```

Create `build.pmake`:

```pmake
pmake_required_version = 1.0
cxx = "g++"

if os == "windows" {
    output = "hello.exe"
} else {
    output = "hello"
}

rule compile_and_link {
    command = "${cxx} -std=c++20 ${in} -o ${out}"
}

build "${output}" : compile_and_link "main.cpp"
alias all : "${output}"
```

Build and run:

```bash
pmake
./hello          # Linux/macOS
.\hello.exe      # Windows PowerShell
```

PMake launches commands directly; shell operators such as `>` and `|` are not
interpreted unless you explicitly invoke a shell.

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

build "main.o" : cc "src/main.cpp"

if os == "windows" {
    app = "app.exe"
} else {
    app = "app"
}

build "${app}" : link "main.o"
alias all : "${app}"
```

Run:

```bash
pmake           # build
pmake -j 8      # parallel
pmake -n        # dry-run
pmake -t clean  # clean outputs
```

---

## Performance

PMake is designed for monorepo-scale builds. It uses:

- **Thread pool** — workers spawned once, tasks consumed from a queue
- **Depfile prefetch** — all depfile-inputs batched before dirty-check
- **Memoized `isDirty`** — one check per node, not per consumer
- **StringPool `DepsLog`** — `vector<uint32_t>` ids instead of `vector<string>`

Benchmark script: `bench_pmake.py` in the test suite.

---

## CMake / Ninja interop

PMake reads CMake-generated `build.ninja` directly:

```bash
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cd build
pmake                              # instead of: ninja
```

- Supported: `rule`, `build`, `default`, `subninja`, `depfile`, `deps=gcc`,
  `rspfile`, `restat`, `generator`, `pool=console`, and Windows drive letters.
- Limitations include implicit outputs, `dyndep`, custom named pools, and
  MSVC dependency scanning. PMake also launches commands directly, without an
  implicit shell.

PMake does not currently export `compile_commands.json`. For CMake projects,
generate it with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`; use PMake to run the
generated build graph.

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
| Foreach             | `foreach f in ["a.cpp", "b.cpp"] { ... }` |
| Function            | `function add_flag(x) { flags += x }` |

Full syntax: [`docs/SYNTAX.md`](docs/SYNTAX.md).

---

## Command-line

```
pmake [options] [targets...]
pmake -t TOOL [target]

Options:
  -V, --version           show version
  -f, --file <file>       build file (default: build.pmake, then build.ninja)
  -j, --jobs <N>          parallel jobs
  -t, --tools <tool>      run a tool (clean, targets, commands)
  --config <name>         build configuration (debug/release)
  --platform <name>       target platform
  -v, --verbose           verbose output (per-task description/command/workdir)
  -q, --quiet             minimal output
  -n, --dry-run           show what would be built
  --no-color              disable colors
  -h, --help              help
```

Every positional argument is a target from the build manifest; multiple targets can be requested.
Use `pmake <target>` for manifest targets and `pmake -t clean` for the utility.

Full CLI reference: [`docs/CLI.md`](docs/CLI.md).

---

## Building and testing

```bash
# Build (recommended)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# → build/pmake (Linux/macOS), build/pmake.exe (Windows)

# Run the test suite
cd tests-pmake/basic
# Windows
python run.py --pmake ../../build/pmake.exe
# Linux/macOS
python run.py --pmake ../../build/pmake
```

The current suite contains **149 tests**. CI builds and tests on Windows and Linux.

---

## Documentation

- Full language reference: [`readme.txt`](readme.txt) (Russian)
- Syntax quick reference: [`docs/SYNTAX.md`](docs/SYNTAX.md)
- CLI reference: [`docs/CLI.md`](docs/CLI.md)
- Ninja compatibility: [`docs/NINJA_COMPAT.md`](docs/NINJA_COMPAT.md)
- Getting started: [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md)
- Troubleshooting: [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md)
- Changelog: [`CHANGELOG.md`](CHANGELOG.md)

---

## Project status

- **Version:** 1.3.1
- **Release prep:** 2026-11-19; tag and publication are pending.
- **Validation:** 149 / 149 tests passing on Windows; CI also covers Linux.
- **Scope:** stable build engine with documented Ninja compatibility limits.
- **Next:** v1.4.0 planning — repository fetching, module namespaces, and a
  `compile_commands.json` exporter.

See [`CHANGELOG.md`](CHANGELOG.md) for the v1.3.1 changes and
[`docs/ROADMAP.md`](docs/ROADMAP.md) for planned work.

---

## License

Licensed under the **Apache License 2.0** — see [`LICENSE`](LICENSE).

---

## About

Developed by **PikMode**.

Contributions, issues, and feature requests are welcome.
