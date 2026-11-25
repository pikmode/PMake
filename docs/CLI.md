# CLI Reference

```
pmake [options] [targets...]
pmake -t TOOL [target]
```

## Tools

| Tool | Action |
|---|---|
| `clean [target]` | Remove build outputs, optionally only the selected target's outputs |
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
| `-j N`, `--jobs N` | Number of parallel jobs. Defaults to detected hardware concurrency |
| `-t TOOL`, `--tools TOOL` | Run a utility tool |
| `--config NAME` | Configuration (e.g. `debug`, `release`). Default: `release` |
| `--platform NAME` | Target platform. Default: detected (`windows`/`linux`/`darwin`) |
| `-n`, `--dry-run` | Show what would be done, don't execute |
| `-v`, `--verbose` | Verbose output: command, workdir, tool details |
| `-q`, `--quiet` | Minimal output (errors only) |
| `--no-color` | Disable ANSI colors (respects `NO_COLOR` env) |
| `-V`, `--version` | Show version |
| `-h`, `--help` | Show help |

The `-f` option selects a manifest; it does not change the working directory.
For a generated Ninja build, run PMake from the build directory (or change to
that directory first) so relative paths resolve as expected.

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

# List targets and inspect the commands for a target
pmake -t targets
pmake -t commands app

# Clean outputs (source files are safe)
pmake -t clean

# From the CMake build directory
pmake -j 8
```

## Environment variables

| Variable | Effect |
|---|---|
| `NO_COLOR` | Disable colors when the variable is set |
