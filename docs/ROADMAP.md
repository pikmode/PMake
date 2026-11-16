# Roadmap

## v1.3.1 (next)

- **`compile_commands.json` exporter** — `pmake --export-compile-commands`
  or `pmake compdb`. Unlocks clangd / VS Code / CLion / clang-tidy.
- **`Cache::store` no-copy** — avoid doubling `builds_` on every save
  (~700 MB peak reduction on large projects).
- **Abort cleanup** — remove partial `.obj` on Ctrl+C / task failure
  (fixes "undefined reference" after interrupted build).
- **Data-race fixes** — `finished_` / `success_` / `aborted_` are read
  under different mutexes; UB on ARM64.
- **Restart-loop fix** — `generator + restat` combination can cause
  infinite `Re-running CMake...` loop.

## v1.4.0

- **`add_repository(url)`** — FetchContent-like module fetching.
  Decentralized (git clone), no central registry.
  `pkg.pmake` manifest for includes/libs.
- **Module namespaces** — explicit `export`, `private` in modules.
- **Package versioning** — `include "android@1.0.0"`.

## Deferred (nice-to-have, low impact)

- **Arena allocator** for AST nodes — parser is ~2× slower than ninja
  due to AST; arena gives ×3–5 faster AST gen, but parsing is <1%
  of total build time at scale.
- **`mmap` I/O** — only helps multi-hundred-MB ninja files.
- **Heterogeneous lookup** in parser `StringPool` — small win.
- **Distributed builds / remote execution** — explicitly not a goal
  until `compile_commands.json` ships and users ask for it.

## Won't fix (by design)

- **`#` comments** — PMake uses `//` and `/* */`. Not negotiable.
- **`pmake clean` deleting source** — fixed in v1.3.0, will never
  regress (defense in depth: `is_protected_source()`).
- **Full Chromium / LLVM builds** — technically possible, but 350K
  edges at 10 cores = many hours. PMake's target is **incremental**
  and **partial** builds, not full rebuilds of huge monorepos.
