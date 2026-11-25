# Ninja Compatibility

PMake reads Ninja manifests directly, without conversion or glue scripts.
Compatibility is strongest for CMake-generated manifests; manifests from other
generators work when they use features in the supported subset below.

## Supported

| Feature | Status |
|---|---|
| `rule` | ✅ |
| `build` | ✅ |
| `default` | ✅ (only the first target is selected if several are listed) |
| `include` / `subninja` | ✅ |
| `$in`, `$out`, `${var}` | ✅ |
| Escapes: `$:`, `$ `, `$$` | ✅ |
| Multi-line `$<newline>` | ✅ |
| `\|` (implicit inputs) | ✅ |
| `\|\|` (order-only) | ✅ |
| `depfile` | ✅ |
| `deps = gcc` | ✅ |
| `deps = msvc` | ✅ Parses `Note: including file:` output (command must use `/showIncludes`) |
| `rspfile` / `rspfile_content` | ✅ |
| `restat` | ✅ |
| `generator` | ✅ |
| `pool = console` | ✅ (serializes tasks; Ninja's console I/O behavior is not reproduced) |
| `phony` edges (no separate rule declaration needed; inputs may be empty) | ✅ |
| Windows drive letters (`E$:/`) | ✅ |

## Not yet supported

| Feature | Impact |
|---|---|
| Implicit outputs (`\|` before `:`) | Ignored; graphs that rely on them may be incomplete |
| `dyndep` | Dynamic dependency updates are unsupported (for example, C++20 modules) |
| Custom named pools | Rule-level assignments to named pools are not supported; affected tasks may use the default pool |

## How to use

```bash
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cd build
pmake
# Or explicitly select the manifest from this directory:
pmake -f build.ninja
```

PMake resolves relative paths from the current working directory. Running from
the build directory keeps paths in the generated Ninja file consistent.

## What's different from `ninja`

- **Cache**: PMake has its own binary cache (`Project-cache.pmake`),
  not `.ninja_deps` / `.ninja_log`. When its cache is missing or invalid,
  PMake re-scans available GCC depfiles. MSVC `/showIncludes` dependencies
  have no depfile to re-scan, so affected commands run again.
- **No-op builds**: when outputs and recorded dependencies are current, PMake
  skips the commands and reports `All targets up to date`.
- **Compiler output**: standard error is forwarded even when a command
  succeeds (so warnings are visible). MSVC include-trace lines are filtered
  when `deps = msvc` is enabled.
- **Manifest regeneration**: CMake-generated manifests can contain an edge
  that reruns CMake when its inputs change. PMake runs that edge and restarts
  after the manifest is regenerated.
- **`-t clean`**: PMake removes eligible outputs declared in the currently
  parsed manifest (skipping phony/generator edges and protected source files);
  it does not use `.ninja_log`. It may not find outputs from manifests or
  configurations that are no longer active.
- CMake's `clean` and `help` targets invoke Ninja's `CLEAN` / `HELP` rules.
  PMake runs these command-only rules without requiring a file named after
  the target and forwards their output.
- **`compile_commands.json`**: PMake does not export it yet. CMake can generate
  it separately with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
- **Command execution**: PMake launches the executable and arguments directly,
  rather than invoking a shell for each Ninja command. Commands that rely on
  shell operators or shell expansion may need an explicit shell invocation.
