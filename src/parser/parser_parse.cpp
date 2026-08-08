#include "parser/parser_impl.h"
#include <QSet>
#include <QFileInfo>

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
        if (!errors_.isEmpty()) {
            for (const QString& err : errors_) {
                LOG_ERROR(err);
            }
            return nullptr;
        }
        if (hasError_) {
            LOG_ERROR("Skipping AST evaluation due to parse errors.");
        }

        evaluateAst(root_);

    } catch (const std::exception& e) {
        LOG_ERROR(QString("Parsing failed: %1").arg(e.what()));
        return nullptr;
    } catch (...) {
        LOG_ERROR("Unknown exception during parsing");
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