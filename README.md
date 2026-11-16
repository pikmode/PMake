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
- **Scales to monorepos.** Thread pool + depfile prefetch + StringPool DepsLog — designed for 100K+ edges.
- **Safe `clean`.** `pmake clean` never touches source files (`.cpp`, `.cmake`, `CMakeLists.txt`).

---

## Quick start

### Build from source

**Recommended: CMake + Ninja.**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# → build/pmake (Linux/macOS), build/PMake.exe (Windows)
```

**Or self-host with PMake** (requires a v1.2.0+ binary):

```bash
pmake -f build.pmake
# → PMake.exe (Windows) / pmake (Linux/macOS)
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
pmake -f build/build.ninja        # instead of: ninja -C build
```

- Supported: `rule`, `build`, `pool`, `subninja`, `default`, `depfile`,
  `deps=gcc`, `rspfile`, `restat`, `generator`, `pool=console`,
  Windows drive letters (`E$:/path` — the CMake escaping)
- Not yet: implicit outputs (`|` before `:`), `dyndep`

**Note:** `compile_commands.json` for clangd/IDE is planned for v1.3.1.
Until then, use Ninja for IDE tooling and PMake for builds.

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

Full syntax: [`docs/SYNTAX.md`](docs/SYNTAX.md).

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

Full CLI reference: [`docs/CLI.md`](docs/CLI.md).

---

## Building and testing

```bash
# Build (recommended)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# → build/pmake (Linux/macOS), build/PMake.exe (Windows)

# Run the test suite (Windows)
cd tests-pmake-1.1/windows
python run.py --pmake ../../build/PMake.exe

# Run the test suite (Linux/macOS)
cd tests-pmake-1.1/linux
python run.py --pmake ../../build/pmake
```

Expected: **134 / 134 passed, 0 regressions**.

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

- **Version:** 1.3.0 "Performance & Scale"
- **Tests:** 134 / 134 passing (Windows + MinGW; Linux/macOS in CI)
- **Maturity:** stable; designed for 100K+ edges
- **Roadmap:**
  - **v1.3.1** — `compile_commands.json` exporter (clangd/VS Code/CLion),
    `Cache::store` no-copy, abort-cleanup for partial `.obj`, data-race
    fixes for ARM64
  - **v1.4.0** — `add_repository(url)` (FetchContent-like), module namespaces

See [`CHANGELOG.md`](CHANGELOG.md) for what's new in v1.3.0 and known issues.

---

## License

Licensed under the **Apache License 2.0** — see [`LICENSE`](LICENSE).

---

## About

Developed by **PikMode**.

Contributions, issues, and feature requests are welcome.
