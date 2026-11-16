# Architecture (for contributors)

## Pipeline

```
build.pmake / build.ninja
        │
        ▼
    [Parser]
        │
        ▼
      State  (rules, builds, variables, functions)
        │
        ▼
      Graph  (nodes, edges, cycles check)
        │
        ▼
   Dirty detection (mtime + depfile + command hash)
        │
        ▼
    Scheduler (toposort, pool counters, thread pool)
        │
        ▼
     Executor (spawn compilers, capture output)
        │
        ▼
    Cache save (state + file states + command hashes)
```

## Key files

| Path | Role |
|---|---|
| `src/main.cpp` | CLI parsing, orchestration, `clean` command |
| `src/parser/parser_*` | PMake DSL parser (lexer → AST → eval → State) |
| `src/parser/ninja-parser/*` | Ninja parser (direct to State, no AST) |
| `src/core/state.*` | Project state: rules, builds, vars, functions |
| `src/core/graph.*` | Node graph, cycle detection, topological sort |
| `src/core/node.*` | One graph node, `isDirty()`, `build()` |
| `src/core/engine*` | Scheduler + thread pool + executor coordination |
| `src/core/cache.*` | Binary cache (`Project-cache.pmake`) |
| `src/deps/*` | `.d` file parser, `DepsLog` (header tracking) |
| `src/executor/runner.*` | Process spawn (POSIX fork/exec, Win CreateProcessW) |
| `src/util/*` | Filesystem, string utils, hashing, logging |

## Thread pool

`Engine` spawns `maxJobs + 4` workers once. Tasks are dispatched via
`workQueue_`. Each worker runs `workerRoutine(node)` → `node->build()`.
Completion flows back through `completed_` queue to the main thread.

Why `+4`: slack for `console` pool, custom pools, and race conditions.

## Dirty detection

For each node, in order:

1. `forceRebuild` flag (config/platform change) → dirty
2. `generator` rule → special logic (skip if outputs exist and no newer inputs)
3. Outputs missing → dirty
4. Any input newer than max output mtime → dirty
5. Depfile header newer → dirty
6. Command hash changed (cache invalid) → dirty
7. BFS propagate: if A dirty → all downstream of A in plan

`collectDirtyNodes` prefetches all relevant paths into `statBatch` first,
then memoizes `isDirty()` results — one check per node.

## Parser (DSL)

`Lexer` → tokens. `ParserImpl` → AST via recursive descent. `evaluateAst`
walks the AST, calling handlers like `evaluateIf`, `evaluateForeach`,
`evaluateBuild`. Handlers can mutate `State`.

Includes are deferred: `parseInclude` builds an AST node, `evaluateInclude`
reads and parses the sub-file, then evals its AST in the parent scope.

## Parser (Ninja)

Direct scan: tokens → `State` mutation. No AST. One pass.
Fast-path in `parseName()` for the common case (single IDENT/STRING
without `$`).

## DepsLog

Header paths are interned into a `StringPool` (`std::deque<std::string>`).
`DepsEntry` stores `vector<uint32_t>` ids — one 4-byte int per header,
not a 32-byte string. On Chromium scale this is ~8× less RAM.

## Cache format

Binary, version-tagged. Current version: **9**.

Layout: magic → version → globals → rules → builds → pools → targets →
file states → command hashes → DepsLog entries.

Version mismatch → cache rejected with warning, full rebuild.

## Adding a new builtin

1. Add function in `src/core/builtins.cpp`
2. Add `if (name == "your_builtin") return yourFunction(args);` in `Builtins::call`
3. Add test in `tests-pmake-1.1/src/tests_builtins.py`
4. Update `docs/SYNTAX.md` builtins table

## Adding a new DSL statement

1. `TokenType` in `lexer.h` (if new token needed)
2. `AstNode::Type` in `ast.h`
3. Parse in `parser_dispatch.cpp::parseStatement`
4. Eval in `parser_eval.cpp::evaluateAst` switch
5. Test in `tests-pmake-1.1/`
