# Roadmap

## v1.3.2 (release candidate)

Prepared 2026-11-24. See [`CHANGELOG.md`](../CHANGELOG.md) for fixes and
validation details. Tagging and publication are not part of this preparation.

## v1.4.0 (planned)

- **`add_repository(url)`** — FetchContent-like module fetching.
  Decentralized (git clone), no central registry.
  `pkg.pmake` manifest for includes/libs.
- **Module namespaces** — explicit `export`, `private` in modules.
- **Package versioning** — `include "android@1.0.0"`.
- **`compile_commands.json` exporter** — planned for IDE tooling; not
  currently provided by PMake.

## Deferred (nice-to-have, low impact)

- **Arena allocator** for AST nodes — parser is ~2× slower than ninja
  due to AST; arena gives ×3–5 faster AST gen, but parsing is <1%
  of total build time at scale.
- **`mmap` I/O** — only helps multi-hundred-MB ninja files.
- **Heterogeneous lookup** in parser `StringPool` — small win.
- **Distributed builds / remote execution** — explicitly not a goal unless
  users ask for it.

## Won't fix (by design)

- **`#` comments** — PMake uses `//` and `/* */`. Not negotiable.
- **`pmake -t clean` deleting source** — fixed in v1.3.0, will never
  regress (defense in depth: `is_protected_source()`).
- **Full Chromium / LLVM builds** — technically possible, but 350K
  edges at 10 cores = many hours. PMake's target is **incremental**
  and **partial** builds, not full rebuilds of huge monorepos.
