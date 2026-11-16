# Changelog

All notable changes to PMake. Format based on [Keep a Changelog](https://keepachangelog.com/).

## [1.3.0] — 2026-11-16

**Performance & Scale.** Optimized for LLVM/Chromium-class monorepos (350K+ build edges).

### Performance

- **Thread pool** вместо thread-per-task. Холодный dry-run на 1000 задач: **1080 ms → 208 ms** (×5). На 10K задач — линейно, без OOM.
- **DepsLog StringPool**: `vector<string>` → `vector<uint32_t>`. RAM на Chromium (350K edges × 500 headers): **5.6 GB → 700 MB** (×8).
- **Prefetch depfile-inputs** в `statBatch` — noop больше не делает 175M cold-stat'ов. На 1000 задачах: **204 ms → 172 ms**.
- **Memoize `isDirty`** — 350K edges × 100 consumers × 500 headers → ×17 млрд → ×17.5 млн итераций.
- **Skip `addDepsForOutput` в dry-run** — cold 1000 with-depfiles: **7970 ms → 788 ms** (×10).

### Fixed

- **CRITICAL: `pmake clean` удалял source-файлы**. CMake генерирует phony-edge с inputs в позиции outputs ("missing input files is not an error"). Без пропуска phony `pmake clean` удалял `CMakeLists.txt`, `CMakeCache.txt`, и **всю установку CMake** (200+ `.cmake` модулей). Теперь: `clean` пропускает phony/generator/empty-command + защищает source-расширения (defense in depth).
- **Windows drive letter `E$:` в ninja-парсере**. `parseName` fast-path обрывал `E:/pikmode/...` на `"E"` → "Duplicate output 'E'" × 68 при парсинге CMake-generated `build.ninja`. Теперь корректно возвращает полный путь.
- **Sub-parser счётчики**. `MAX_INCLUDE_DEPTH`, `MAX_OPERATIONS`, `MAX_FUNCTION_CALL_DEPTH` сбрасывались в конструкторе sub-парсера → не работали для include-ов. Перенесено в корневой `ParserImpl::parse()`.
- **Stack-safe limits**. `MAX_INCLUDE_DEPTH` 5000 → **200**, `MAX_STATEMENT_DEPTH` 10000 → **500**, `MAX_EXPR_DEPTH` 10000 → **500**. Старые значения = stack overflow на 3000–4000 уровне include-цепочки.
- **CLI `pmake <target>`**. Первый positional всегда считался командой → `pmake all`, `pmake .generated` падали с "unknown command". Теперь make/ninja-style: неизвестный positional = target.
- **Engine exit codes**. `success_` не сбрасывался в error-path → `pmake nonexistent_target` возвращал exit 0. Теперь exit 1 с "Target not found".
- **Duplicate errors в lexer**. Фатальная ошибка печаталась 3 раза (`Lexer::error` + `advance` + `parse`). Теперь ровно одна строка.
- **`#` вместо `//`**. Generic "Invalid character" → подсказка `'#' is not a comment in PMake. Use '//' for line comments or '/* */' for block comments.`
- **Manifest regen UI**. `Re-running CMake...` (description от `RERUN_CMAKE`) не показывался — regen 30–60 сек выглядел как "зависло". Теперь виден `[1/1] Re-running CMake...`.
- **`All targets up to date` от genEngine**. Regen-noop печатал это ПЕРЕД основным build'ом → путаница. Убрано.

### Changed

- **Cache version 8 → 9**. Формат `DepsEntry` (StringPool + inputIds). Старые `.pmake`-кеши отвергаются с warning'ом — пересборка автоматически.
- **Лимиты ×10–50** от Chromium×2: `MAX_RULES 10M`, `MAX_BUILDS 20M`, `MAX_OPERATIONS 5B`, `MAX_FOREACH_ITERATIONS 10M`.
- **`operationCounter` int → int64_t** (5B не влезает в int).

### Tests

- **117/117 tests** (было 111 в v1.2.0).
- Новые: `error_hash_comment_hint`, `error_hash_not_duplicated`, `hash_inside_string_ok`, `cli_target_positional`, `include_deep_50_works`, `include_depth_limit_201`.

### Known issues (v1.3.1)

- **Partial `.obj` при abort/interrupt**. Ctrl+C или ошибка компиляции → in-flight g++ дописывает `.obj` с новым mtime, pmake думает "up-to-date" → следующий link падает с "undefined reference". Обход: `pmake clean` после сбоя. Фикс — в v1.3.1.
- **`Cache::store` двойная копия** `builds_`. На Chromium ~700 MB лишних + 5 сек/итерацию. Фикс — v1.3.1.
- **Restart-loop при `generator + restat`**. Если regen-таск возвращает exit 2 (restat), `wasNoop()` = false → рестарт → снова regen → вечный цикл. Фикс — v1.3.1.
- **Data race на `finished_`** между `planMutex_` и `completionMutex_`. На x86/MSVC работает, на ARM64 может не работать. Фикс — v1.3.1.

## [1.2.0] — 2026-11-13

- Ninja-style manifest regeneration (auto-restart при изменении generator-файлов).
- Graceful Ctrl+C (clean shutdown).
- Строгая валидация CLI (`-j`, `-f`).
- 111/111 тестов.