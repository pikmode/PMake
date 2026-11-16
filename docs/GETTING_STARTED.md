# Getting Started

Build your first project with PMake in under 5 minutes.

## 1. Install PMake

Download `pmake.exe` (Windows) or `pmake` (Linux/macOS) from
[Releases](https://github.com/user/pmake/releases), drop it somewhere
in your `PATH`. That's it — single static binary, no dependencies.

## 2. Hello world

Create `build.pmake`:

```pmake
pmake_required_version = 1.0

rule echo {
    command = "echo ${msg} > ${out}"
}

build "hello.txt" : echo { msg = "Hello, PMake!" }
alias all : "hello.txt"
```

Run:

```bash
pmake
```

You'll see:

```
[1/1] echo hello.txt
Build succeeded in 0.02 s.
```

## 3. Add a C++ file

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

build "main.o" : cc "main.cpp"
build "app"    : link "main.o"
alias all      : "app"
```

```bash
pmake              # incremental build
pmake -j 8         # 8 parallel jobs
pmake -n           # dry-run
pmake clean        # remove outputs
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

Config change → cache invalidated → full rebuild (automatic).

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
