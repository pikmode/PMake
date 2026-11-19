# Getting Started

Build your first project with PMake in under 5 minutes.

## 1. Install PMake

Download `pmake.exe` (Windows) or `pmake` (Linux/macOS) from
[Releases](https://github.com/pikmode/PMake/releases) and add its directory to
your `PATH`. The release build is statically linked by default when supported
by the toolchain; Python and Qt are not required to run PMake.

## 2. Hello world

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

Run:

```bash
pmake
```

PMake runs the compiler directly; commands are not implicitly interpreted by a
shell. Run `./hello` on Linux/macOS or `.\hello.exe` in Windows PowerShell to
start the program.

## 3. Track header dependencies

```pmake
pmake_required_version = 1.0

cxx   = "g++"
flags = "-std=c++20 -O2"

rule cc {
    command     = "${cxx} ${flags} -c ${in} -o ${out} -MMD -MF ${out}.d"
    depfile     = "${out}.d"
    deps        = "gcc"
}

rule link {
    command = "${cxx} ${in} -o ${out}"
}

if os == "windows" {
    app = "app.exe"
} else {
    app = "app"
}

build "main.o" : cc "main.cpp"
build "${app}" : link "main.o"
alias all      : "${app}"
```

```bash
pmake              # incremental build
pmake -j 8         # 8 parallel jobs
pmake -n           # dry-run
pmake -t clean     # remove outputs
```

## 4. Configurations

```pmake
if config == "debug" {
    flags = "-O0 -g"
} else {
    flags = "-O2 -DNDEBUG"
}
```

```bash
pmake --config debug
pmake --config release
```

Changing the selected configuration invalidates the relevant build cache.

## 5. Include shared rules

Split build logic into modules:

```pmake
// rules.pmake
rule cc {
    command = "g++ -c ${in} -o ${out}"
}
```

```pmake
// build.pmake
include "rules.pmake"
build "main.o" : cc "main.cpp"
```

Included files can use parent's variables (`${PROJ}`, `${OUT_DIR}`),
functions, and rules. Cycle detection built-in.

## Next steps

- [Syntax reference](SYNTAX.md) — variables, rules, loops, functions
- [CLI reference](CLI.md) — all flags
- [Ninja compatibility](NINJA_COMPAT.md) — if you have CMake projects
- [Troubleshooting](TROUBLESHOOTING.md) — when things go wrong
