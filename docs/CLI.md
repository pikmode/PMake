# CLI Reference

```
pmake [options] [command] [target]
```

## Commands

| Command | Action |
|---|---|
| `build [target]` | Build target (default if omitted) |
| `clean` | Remove output files (NOT source files) |

Unknown first positional argument is treated as **target**, not command —
`pmake all` works like `pmake build all`.

## Options

| Option | Description |
|---|---|
| `-f FILE`, `--file FILE` | Build file. Default: `build.pmake`, then `build.ninja` |
| `-j N`, `--jobs N` | Parallel jobs. Default: `nproc` |
| `--config NAME` | Configuration (e.g. `debug`, `release`). Default: `release` |
| `--platform NAME` | Target platform. Default: detected (`windows`/`linux`/`darwin`) |
| `--clean` | Same as `clean` command |
| `-n`, `--dry-run` | Show what would be done, don't execute |
| `-v`, `--verbose` | Verbose output: command, workdir, `clean` details |
| `-q`, `--quiet` | Minimal output (errors only) |
| `--no-color` | Disable ANSI colors (respects `NO_COLOR` env) |
| `-V`, `--version` | Show version |
| `-h`, `--help` | Show help |

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | Build error, parse error, target not found |
| `130` | Interrupted (Ctrl+C) |

## Examples

```bash
# Basic build
pmake

# Build specific target
pmake app
pmake build app

# Debug configuration
pmake --config debug

# Cross-compile for Windows from Linux
pmake --platform windows

# Parallel build, 8 jobs
pmake -j 8

# Dry-run: what would be built?
pmake -n

# Verify everything is up-to-date
pmake -n && echo "up-to-date"

# Clean outputs (source files are safe)
pmake clean

# Build CMake project with PMake
pmake -f build/build.ninja -j 8
```

## Environment variables

| Variable | Effect |
|---|---|
| `NO_COLOR` | Disable colors (any non-empty value) |
| `PMAKE_JOBS` | Default `-j` value (overridden by `-j`) |
