# Changelog

All notable changes to PMake. Format based on [Keep a Changelog](https://keepachangelog.com/).

## [1.3.1] — 2026-11-19

### Added

- Ninja-style utility tools via `-t` / `--tools`: `clean`, `targets`, and `commands`.
- `-t commands [target]` prints expanded build commands in dependency order for the selected target.

### Changed

- All positional arguments are manifest targets, including names such as `build`, `clean`, and `help`.
- Built-in utility actions remain selected with `-t`, for example `pmake -t clean`.
- Cache format version 9 → 10, adding content fingerprints for the main and included manifests.
- Rewrote `readme.txt` as a detailed Russian user guide covering setup, manifest syntax, builds, CLI tools, caching, and Ninja/CMake compatibility.

### Fixed

- Invalidate cached builds when an included manifest changes, and fingerprint response-file content and working directories.
- Preserve dependency edges and command fingerprints for every output of a multi-output build.
- Avoid recursive graph walks on deep dependency chains and prevent no-op `restat` generators from restarting PMake.
- Remove partial outputs after command failures, treat external exit code 2 as a failure, and report `clean` removal errors.
- Quote Windows self-restart arguments, compare Windows file names case-insensitively, and make process spawning safe with concurrent workers.
- Synchronize engine completion state across scheduler and waiter threads.

### Tests

- **149/149 tests** pass, including new regressions for the fixed issues.

## [1.3.0] — 2026-11-16

**Performance & Scale.** Optimized for LLVM/Chromium-class monorepos (350K+ build edges).

### Performance

- **Thread pool** instead of thread-per-task. Cold dry-run with 1,000 tasks: **1080 ms → 208 ms** (5×). Scales linearly to 10K tasks without OOM.
- **DepsLog StringPool**: `vector<string>` → `vector<uint32_t>`. RAM usage on Chromium (350K edges × 500 headers): **5.6 GB → 700 MB** (8×).
- **Prefetch depfile inputs** in `statBatch` — noop builds no longer perform 175M cold stats. With 1,000 tasks: **204 ms → 172 ms**.
- **Memoize `isDirty`** — reduced iterations for 350K edges × 100 consumers × 500 headers from 17 billion to 17.5 million.
- **Skip `addDepsForOutput` in dry-run** — cold run with 1,000 depfile tasks: **7970 ms → 788 ms** (10×).

### Fixed

- **CRITICAL: `pmake clean` deleted source files**. CMake generates phony edges with inputs in the outputs position ("missing input files is not an error"). Without skipping phony edges, `pmake clean` deleted `CMakeLists.txt`, `CMakeCache.txt`, and **the entire CMake installation** (200+ `.cmake` modules). `clean` now skips phony/generator/empty-command edges and protects source extensions as defense in depth.
- **Windows drive letter `E$:` in the Ninja parser**. The `parseName` fast path truncated `E:/pikmode/...` to `"E"`, causing 68 "Duplicate output 'E'" errors when parsing CMake-generated `build.ninja` files. It now returns the full path.
- **Sub-parser counters**. `MAX_INCLUDE_DEPTH`, `MAX_OPERATIONS`, and `MAX_FUNCTION_CALL_DEPTH` were reset in the sub-parser constructor, so they did not work across includes. Resetting now happens in the root `ParserImpl::parse()`.
- **Stack-safe limits**. `MAX_INCLUDE_DEPTH` changed from 5,000 to **200**, `MAX_STATEMENT_DEPTH` from 10,000 to **500**, and `MAX_EXPR_DEPTH` from 10,000 to **500**. The old values could cause stack overflows at include-chain depths of 3,000–4,000.
- **CLI `pmake <target>`**. The first positional argument was always treated as a command, so `pmake all` and `pmake .generated` failed with "unknown command". The CLI now follows make/Ninja conventions: an unknown positional argument is treated as a target.
- **Engine exit codes**. `success_` was not reset on the error path, so `pmake nonexistent_target` returned exit code 0. It now returns exit code 1 with "Target not found".
- **Duplicate lexer errors**. Fatal errors were printed three times (`Lexer::error` + `advance` + `parse`). They are now printed only once.
- **`#` instead of `//`**. A generic "Invalid character" error is now replaced with the hint `'#' is not a comment in PMake. Use '//' for line comments or '/* */' for block comments.`
- **Manifest regeneration UI**. `Re-running CMake...` (the description for `RERUN_CMAKE`) was not displayed, making 30–60 second regeneration look stuck. It is now shown as `[1/1] Re-running CMake...`.
- **`All targets up to date` from `genEngine`**. A no-op regeneration printed this before the main build, causing confusion. This message has been removed.

### Changed

- **Cache version 8 → 9**. Updated the `DepsEntry` format (StringPool + inputIds). Old `.pmake` caches are rejected with a warning and rebuilt automatically.
- **Limits increased by 10–50×** based on Chromium ×2: `MAX_RULES 10M`, `MAX_BUILDS 20M`, `MAX_OPERATIONS 5B`, `MAX_FOREACH_ITERATIONS 10M`.
- **`operationCounter` changed from `int` to `int64_t`** (5B does not fit in an `int`).

### Tests

- **117/117 tests** (up from 111 in v1.2.0).
- New tests: `error_hash_comment_hint`, `error_hash_not_duplicated`, `hash_inside_string_ok`, `cli_target_positional`, `include_deep_50_works`, `include_depth_limit_201`.

## [1.2.0] — 2026-11-13

- Ninja-style manifest regeneration (automatic restart when generator files change).
- Graceful Ctrl+C handling (clean shutdown).
- Strict CLI validation (`-j`, `-f`).
- 111/111 tests.