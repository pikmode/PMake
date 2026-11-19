# CLI Reference

```
pmake [options] [targets...]
pmake -t TOOL [target]
```

## Tools

| Tool | Action |
|---|---|
| `clean` | Remove build outputs without deleting protected source files |
| `targets` | List declared build targets |
| `commands [target]` | Print commands for all builds or a target and its dependencies |
| `list` | List available tools |

Every positional argument is a manifest target. Target names such as `build`,
`clean`, and `help` are not reserved; for example, `pmake build clean` builds
both `build` and `clean`. Built-in utilities are selected only with `-t`.

## Options

| Option | Description |
|---|---|
| `-f FILE`, `--file FILE` | Build file. Default: `build.pmake`, then `build.ninja` |
| `-j N`, `--jobs N` | Parallel jobs. Default: `nproc` |
| `-t TOOL`, `--tools TOOL` | Run a utility tool |
| `--config NAME` | Configuration (e.g. `debug`, `release`). Default: `release` |
| `--platform NAME` | Target platform. Default: detected (`windows`/`linux`/`darwin`) |
| `-n`, `--dry-run` | Show what would be done, don't execute |
| `-v`, `--verbose` | Verbose output: command, workdir, tool details |
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
pmake clean help

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

# List targets and inspect the commands for a target
pmake -t targets
pmake -t commands app

# Clean outputs (source files are safe)
pmake -t clean

# Build CMake project with PMake
pmake -f build/build.ninja -j 8
```

## Environment variables

| Variable | Effect |
|---|---|
| `NO_COLOR` | Disable colors (any non-empty value) |
| `PMAKE_JOBS` | Default `-j` value (overridden by `-j`) |
