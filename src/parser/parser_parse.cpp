#include "parser/parser_impl.h"

#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace pmake::parser {

// ---- parseTopLevel ----
//
// requireVersion = true  → корневой файл: pmake_required_version ОБЯЗАТЕЛЬНА.
// requireVersion = false → include/import: директива ОПЦИОНАЛЬНА,
//                          а если присутствует — просто пропускается
//                          (без валидации версии, без изменения состояния).
void ParserImpl::parseTopLevel(bool requireVersion) {
    advance();
    while (cur_.type == TOK_NEWLINE || cur_.type == TOK_INDENT) advance();

    if (cur_.type == TOK_EOF) {
        error("Empty file", true);
        return;
    }

    if (requireVersion) {
        // ----- Корневой файл: версия обязательна -----
        if (cur_.type != TOK_IDENT || str(cur_.strId) != "pmake_required_version") {
            error("First directive must be 'pmake_required_version = 1.0'", true);
            while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
            if (cur_.type == TOK_NEWLINE) advance();
        } else {
            advance();
            parseVersionDirective();
            if (!versionDeclared_) {
                if (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
                    while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
                    if (cur_.type == TOK_NEWLINE) advance();
                }
            }
        }
    } else {
        // ----- Include/import: версия опциональна и игнорируется -----
        if (cur_.type == TOK_IDENT && str(cur_.strId) == "pmake_required_version") {
            while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
            if (cur_.type == TOK_NEWLINE) advance();
        }
    }

    skipNewlinesAndIndents();

    int loopGuard = 0;
    while (cur_.type != TOK_EOF) {
        if (++loopGuard > MAX_LOOP_GUARD) {
            error("Infinite loop in parser at top level", true);
            break;
        }
        parseStatement(0);
        skipNewlinesAndIndents();
    }
}

// ---- Публичный метод parse (корневой) ----
//
// CHANGED (Issue B): здесь инициализируем статические счётчики.
// Это ЕДИНСТВЕННОЕ место, где они сбрасываются. Sub-парсеры
// (evaluateIncludeImpl) вызывают parseAstOnly() и НЕ трогают счётчики —
// поэтому MAX_INCLUDE_DEPTH / MAX_OPERATIONS / MAX_FUNCTION_CALL_DEPTH
// работают на всём дереве include-ов, а не «на каждый файл отдельно».
//
// До фикса конструктор ParserImpl сбрасывал их в 0, и каждый sub-парсер
// начинал с чистого листа:
//   * includeDepth = 0 → 5001-й include видел depth=1, лимит не срабатывал
//   * после возврата parent делал --includeDepth → -1 (латентный баг)
//   * operationCounter = 5B на файл → на Chromium с 2500 include это
//     12.5 трлн операций «разрешено», лимит фактически не работал
std::unique_ptr<core::State> ParserImpl::parse() {
    // RESET — ОДИН раз на корневой parse. Не переносить в конструктор.
    ParserImpl::operationCounter  = 0;
    ParserImpl::includeDepth      = 0;
    ParserImpl::functionCallDepth = 0;

    try {
        parseTopLevel();  // requireVersion = true (default)
        if (!errors_.empty()) {
            for (const auto& err : errors_) {
                std::println(std::cerr, "{}", err);
            }
            return nullptr;
        }
        if (hasError_) {
            std::println(std::cerr, "Skipping AST evaluation due to parse errors.");
        }

        evaluateAst(root_);

    } catch (const std::exception& e) {
        std::println(std::cerr, "Parsing failed: {}", e.what());
        return nullptr;
    } catch (...) {
        std::println(std::cerr, "Unknown exception during parsing");
        return nullptr;
    }

    if (!state_.getRule("phony")) {
        core::Rule phony;
        phony.name = "phony";
        phony.command = "";
        state_.addRule(std::move(phony));
    }
    return std::make_unique<core::State>(std::move(state_));
}

// ============================================================
// parseAstOnly
//
// Только строит AST в root_. Никакого eval. Используется в
// evaluateIncludeImpl для sub-файлов: sub-файл парсится, но его
// AST eval'ится в parent ParserImpl (чтобы видеть parent globals).
//
// requireVersion = false → sub-файл не обязан содержать
// pmake_required_version, а если содержит — она игнорируется.
//
// CHANGED (Issue B): НЕ сбрасываем статические счётчики здесь.
// Sub-парсер должен видеть parent's includeDepth / operationCounter /
// functionCallDepth и продолжать их инкрементить. Иначе лимиты
// работают «на каждый файл», а не на всё дерево.
// ============================================================
void ParserImpl::parseAstOnly() {
    try {
        parseTopLevel(/*requireVersion=*/false);
    } catch (const std::exception& e) {
        std::println(std::cerr, "Parsing failed: {}", e.what());
    } catch (...) {
        std::println(std::cerr, "Unknown exception during parsing");
    }
}

} // namespace pmake::parser