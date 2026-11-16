# Troubleshooting

## `pmake clean` removed my files (v1.2.x)

**Fixed in v1.3.0.** If you're on v1.2.x, upgrade immediately.

The old `clean` iterated `build->outputs` and removed everything except
`generator` rules. CMake puts **all input files** in the outputs position
of a `phony` edge (the "missing input files is not an error" trick),
so `pmake clean` on a CMake project deleted:

- `CMakeLists.txt` (source!)
- `CMakeCache.txt`, `CMakeFiles/`
- The entire CMake installation (~200 `.cmake` modules)

**Symptom after clean:** CMake says `Could not find cmake module file: ...`
or `System is unknown to cmake: Platform/Windows`.

**Fix:** reinstall CMake, then upgrade PMake to v1.3.0+.

## `Duplicate output 'X'` on CMake-generated `build.ninja`

**Fixed in v1.3.0.** Windows drive letters like `E$:/path` were being
truncated to `E` by the ninja parser's fast-path.

**Symptom:** `Duplicate output 'E'` repeated N times, then `pmake: error`.

**Fix:** upgrade to v1.3.0+.

## `STATUS_STACK_OVERFLOW` on deep includes

**Fixed in v1.3.0.** Old `MAX_INCLUDE_DEPTH = 5000` needed ~5 MB stack;
default Windows stack is 1–8 MB.

**Fix:** upgrade to v1.3.0 (limit is now 200, safe for real projects).
If you need more: `/STACK:16777216` in MSVC, `-Wl,-z,stack-size=...` in GCC.

## `undefined reference` after a failed build

**Known issue, v1.3.1 fix pending.**

When a build is interrupted (Ctrl+C or a task failure), in-flight
compilers may finish writing `.obj` files **after** PMake gives up.
These have a fresh mtime, so PMake thinks "up-to-date" on next run.
Result: link fails with `undefined reference` to a symbol that should
exist.

**Workaround:** `pmake clean` before rebuilding.

**Tracked:** v1.3.1 will remove partial outputs on abort.

## `Cache version mismatch (8 != 9)`

**Normal after upgrade to v1.3.0.** Cache format changed (StringPool +
`inputIds`). Old cache is rejected, PMake rebuilds it automatically.

First run after upgrade: full `.d` re-scan (slow). Subsequent runs:
fast.

## `#` fails with "not a comment"

PMake uses `//` and `/* */` comments, not `#`. If you copy from a
Makefile or shell script, replace `#` with `//`.

The error message will hint: `'#' is not a comment in PMake. Use '//' for line comments or '/* */' for block comments.`

## `pmake <target>` doesn't work

**Fixed in v1.3.0.** In v1.2.x, the first positional argument was always
treated as command, so `pmake all` failed with "unknown command 'all'".

**Fix:** upgrade to v1.3.0.

## Build hangs on `Re-running CMake...`

If you see this line and no progress for >2 minutes:

1. `Ctrl+C`
2. `cmake --fresh -S . -B build -G Ninja` — regenerate from scratch
3. `pmake -f build/build.ninja`

Usually caused by a corrupted `CMakeCache.txt` or a missing CMake module.

## Out of memory on large projects

PMake v1.3.0 is designed for 100K+ edges. If you're hitting OOM:

1. **Check RAM usage**: `pmake -n` (dry-run) should peak well below
   your available RAM. If not, report an issue with the project size.
2. **Reduce parallelism**: `-j 1` or `-j 2` for low-RAM machines.
3. **Cache**: on first run, PMake re-scans all `.d` files. This is slow
   but one-time. Subsequent runs read from `Project-cache.pmake`.

## Where to report bugs

- GitHub Issues: include `pmake -V` output, `build.pmake` (minimal repro),
  and full stderr
- If crash: attach stack trace if available
