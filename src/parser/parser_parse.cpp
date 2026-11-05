#include "parser/parser_impl.h"

#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace pmake::parser {

// ---- parseTopLevel ----
void ParserImpl::parseTopLevel() {
    advance();
    while (cur_.type == TOK_NEWLINE || cur_.type == TOK_INDENT) advance();

    if (cur_.type == TOK_EOF) {
        error("Empty file", true);
        return;
    }

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

    skipNewlinesAndIndents();

    int loopGuard = 0;
    while (cur_.type != TOK_EOF) {
        if (++loopGuard > 50000) {
            error("Infinite loop in parser at top level", true);
            break;
        }
        parseStatement(0);
        skipNewlinesAndIndents();
    }
}

// ---- Публичный метод parse ----
std::unique_ptr<core::State> ParserImpl::parse() {
    try {
        parseTopLevel();
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

} // namespace pmake::parser