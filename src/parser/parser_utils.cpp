#include "parser/parser_utils.h"
#include "internal/config.h"

#include <format>
#include <string>
#include <string_view>

namespace pmake::parser {

double getPmakeVersion() {
    // Единый источник правды — internal/config.h.
    // Никаких version.txt, никаких warning'ов в stderr.
    return PMAKE_VERSION_NUM;
}

std::string formatError(std::string_view filename, int line, int column,
                        std::string_view msg, std::string_view token) {
    if (token.empty())
        return std::format("{}:{}:{}: error: {}", filename, line, column, msg);
    else
        return std::format("{}:{}:{}: error: {}\n    {}", filename, line, column, msg, token);
}

// NEW (#4): глубокая копия AST-узла со всеми полями и потомками.
// Используется в evaluateIncludeImpl для передачи functions из sub в parent.
core::AstNode* cloneAst(core::AstNode* n) {
    if (!n) return nullptr;
    auto c = new core::AstNode();
    c->type            = n->type;
    c->name            = n->name;
    c->value           = n->value;
    c->vars            = n->vars;
    c->loopVar         = n->loopVar;
    c->loopList        = n->loopList;
    c->isExecuted      = n->isExecuted;
    c->line            = n->line;
    c->column          = n->column;
    c->isAppend        = n->isAppend;
    c->op              = n->op;
    c->params          = n->params;
    c->outputCount     = n->outputCount;

    c->children.reserve(n->children.size());
    for (auto ch : n->children) c->children.push_back(cloneAst(ch));

    c->attributes.reserve(n->attributes.size());
    for (auto a : n->attributes) c->attributes.push_back(cloneAst(a));

    c->left              = cloneAst(n->left);
    c->right             = cloneAst(n->right);
    c->body              = cloneAst(n->body);
    c->implicitInputs    = cloneAst(n->implicitInputs);
    c->orderOnlyInputs   = cloneAst(n->orderOnlyInputs);

    return c;
}

} // namespace pmake::parser