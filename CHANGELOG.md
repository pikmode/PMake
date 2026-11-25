# Changelog

All notable changes to PMake are documented here. This changelog follows
[Keep a Changelog](https://keepachangelog.com/).

## [1.3.2] — 2026-11-24

### Added

- MSVC header dependency tracking with `deps = "msvc"` when compile commands
  enable `/showIncludes`, including English and Russian include-trace prefixes.

### Changed

- Stream stdout and stderr from every build command as it runs, including
  failed commands; print the startup logo only when at least one task runs.
- Allow `pmake -t clean <target>` to remove only that target's build outputs,
  leaving its inputs and dependency outputs intact.
- Show compiler warnings and other `stderr` output even when build commands
  succeed.
- Require a fresh, valid GCC-style depfile after a successful command; stale
  depfiles are removed before execution, and valid empty depfiles are accepted.
- Document MSVC dependency tracking, depfile validation, and no-op build behavior.

### Tests

- All **161 tests passed**, including live command-output, rigorous depfile,
  MSVC, warning-output,
  no-op, restat, and stress regressions. The fanout regression verifies stable
  no-op invocation counts and exact incremental rebuilds across 12 compile
  outputs and their shared link target.

## [1.3.1] — 2026-11-19

### Added

- Ninja-style utility tools selected with `-t` / `--tools`: `clean`, `targets`,
  and `commands`.
- `-t commands [target]` prints expanded build commands in dependency order for
  the selected target.

### Changed

- Treat all positional arguments as manifest targets, including names such as
  `build`, `clean`, and `help`. Select built-in utility actions with `-t`, for
  example, `pmake -t clean`.
- Incremented the cache format from version 9 to 10 and added content
  fingerprints for the main and included manifests.
- Rewrote `readme.txt` as a detailed Russian user guide covering setup,
  manifest syntax, builds, CLI tools, caching, and Ninja/CMake compatibility.

### Fixed

- Invalidate cached builds when an included manifest changes; include
  response-file contents and working directories in command fingerprints.
- Preserve dependency edges and command fingerprints for every output of a
  multi-output build.
- Avoid recursive graph walks for deep dependency chains and prevent no-op
  `restat` generators from restarting PMake.
- Remove partial outputs after command failures, treat external exit code 2 as
  a failure, and report errors when `clean` cannot remove an output.
- Removed the success summary from dry-run output.
- Quote Windows self-restart arguments, compare Windows file names
  case-insensitively, and make process spawning safe with concurrent workers.
- Synchronize engine completion state between scheduler and waiter threads.

### Tests

- All **149 tests passed**, including regression tests for the issues fixed in
  this release.

## [1.3.0] — 2026-11-16

Optimized for large codebases such as LLVM and Chromium, with build graphs
containing hundreds of thousands of edges.

### Performance

- Replaced one thread per task with a thread pool. A cold dry run with 1,000
  tasks improved from **1080 ms to 208 ms** (5×); the pool also scales to
  10,000 tasks without running out of memory.
- Replaced `vector<string>` with `vector<uint32_t>` IDs in the DepsLog
  StringPool. In the Chromium workload (350,000 edges × 500 headers), memory
  use fell from **5.6 GB to 700 MB** (8×).
- Prefetched depfile inputs in `statBatch`. For 1,000 tasks, a no-op build
  improved from **204 ms to 172 ms**.
- Memoized `isDirty`, reducing checks in a 350,000-edge workload from
  17 billion to 17.5 million iterations.
- Skipped `addDepsForOutput` during dry runs. A cold run with 1,000 depfile
  tasks improved from **7,970 ms to 788 ms** (10×).

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