#include "parser/parser_impl.h"
#include "parser/parser_utils.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pmake::parser {

// ---- parseFunction ----
void ParserImpl::parseFunction(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::FUNCTION_DEF;
    node->name = parseIdent();
    if (node->name.empty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(node->name, definedFunctions_, "Function")) return;
    addDefinition(node->name, definedFunctions_);

    expect(TOK_LPAREN, "Expected '('");
    std::vector<std::string> params;
    while (cur_.type != TOK_RPAREN && cur_.type != TOK_EOF) {
        if (cur_.type == TOK_IDENT) {
            params.emplace_back(str(cur_.strId));
            advance();
        } else {
            error("Expected parameter name", true);
            break;
        }
        if (cur_.type == TOK_COMMA) advance();
    }
    expect(TOK_RPAREN, "Expected ')'");
    if (cur_.type == TOK_NEWLINE) advance();

    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block for function body", true);
        return;
    }

    auto bodyBlock = makeStringLiteral("");
    bodyBlock->type = AstNode::BLOCK;
    pushBlock(bodyBlock);
    parseBlockDirect(bodyBlock);
    popBlock();

    core::FunctionDef func;
    func.name = node->name;
    func.params = params;
    func.body = cloneAst(bodyBlock);   // CHANGED: используем free-функцию из utils
    state_.addFunction(func);

    node->params = params;
    node->body = bodyBlock;
    parent->children.push_back(node);
}

// ============================================================
// CHANGED (#4): parseInclude — только строит AST-узел с путём.
//
// Никакого чтения файла, никакого sub-парсинга, никаких side-effects.
// Всё это перенесено в evaluateInclude (фаза eval), чтобы:
//   * include внутри "if false" вообще не выполнялся (#4b)
//   * sub-AST eval'ился в parent state_ (видел parent globals) (#4a)
// ============================================================
void ParserImpl::parseInclude(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::INCLUDE;
    node->line = cur_.line;
    node->column = cur_.column;

    auto pathNode = parseValue();
    node->children.push_back(pathNode);
    if (cur_.type == TOK_NEWLINE) advance();

    parent->children.push_back(node);
    skipNewlinesAndIndents();
}

// ---- parseImport — аналогично parseInclude ----
void ParserImpl::parseImport(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::IMPORT;
    node->line = cur_.line;
    node->column = cur_.column;

    auto pathNode = parseValue();
    node->children.push_back(pathNode);
    if (cur_.type == TOK_NEWLINE) advance();

    parent->children.push_back(node);
    skipNewlinesAndIndents();
}

// ---- parseNamespace ----
void ParserImpl::parseNamespace(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::NAMESPACE;
    node->name = parseIdent();
    if (node->name.empty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(node->name, definedNamespaces_, "Namespace")) return;
    addDefinition(node->name, definedNamespaces_);

    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block for namespace", true);
        return;
    }

    auto bodyBlock = makeStringLiteral("");
    bodyBlock->type = AstNode::BLOCK;
    pushBlock(bodyBlock);
    parseBlockDirect(bodyBlock);
    popBlock();
    node->children.push_back(bodyBlock);

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endnamespace") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.push_back(node);
}

// ---- parseConst ----
void ParserImpl::parseConst(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::CONST_DECL;
    node->name = parseIdent();
    if (node->name.empty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (cur_.type != TOK_EQUALS) {
        error("Expected '=' after const name", true);
        return;
    }
    advance();
    auto val = parseValueSequence(true);
    node->children.push_back(val);
    node->line = cur_.line;
    node->column = cur_.column;
    parent->children.push_back(node);
    skipNewlinesAndIndents();
}

// ---- parseEnum ----
void ParserImpl::parseEnum(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::ENUM_DECL;
    node->name = parseIdent();
    if (node->name.empty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(node->name, definedEnums_, "Enum")) return;
    addDefinition(node->name, definedEnums_);

    if (cur_.type != TOK_LBRACE) {
        error("Expected '{' after enum name", true);
        return;
    }
    advance();

    std::vector<AstNode*> enumerators;
    while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
        if (cur_.type == TOK_IDENT) {
            std::string name = parseIdent();
            auto item = makeStringLiteral(name);
            enumerators.push_back(item);
            if (cur_.type == TOK_COMMA) advance();
        } else {
            error("Expected enumerator name", true);
            break;
        }
    }
    expect(TOK_RBRACE, "Expected '}'");
    node->children = std::move(enumerators);
    node->line = cur_.line;
    node->column = cur_.column;

    if (cur_.type == TOK_NEWLINE) advance();
    parent->children.push_back(node);
}

} // namespace pmake::parser