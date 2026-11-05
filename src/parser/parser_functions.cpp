#include "parser/parser_impl.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pmake::parser {

// Глубокое клонирование AST — независимая копия для FunctionDef::body
static AstNode* cloneAst(AstNode* n) {
    if (!n) return nullptr;
    auto c = new AstNode();
    c->type = n->type;
    c->name = n->name;
    c->value = n->value;
    c->vars = n->vars;
    c->loopVar = n->loopVar;
    c->loopList = n->loopList;
    c->isExecuted = n->isExecuted;
    c->line = n->line;
    c->column = n->column;
    c->isAppend = n->isAppend;
    c->op = n->op;
    c->params = n->params;
    c->outputCount = n->outputCount;
    for (AstNode* ch : n->children) c->children.push_back(cloneAst(ch));
    for (AstNode* a : n->attributes) c->attributes.push_back(cloneAst(a));
    c->left = cloneAst(n->left);
    c->right = cloneAst(n->right);
    c->body = cloneAst(n->body);
    c->implicitInputs = cloneAst(n->implicitInputs);    // NEW
    c->orderOnlyInputs = cloneAst(n->orderOnlyInputs);  // NEW
    return c;
}

// Читает файл целиком. TODO: заменить на pikrt::fs::read_all.
static std::optional<std::string> read_include_file(const std::string& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end);
    std::streampos size = f.tellg();
    if (size < 0) return std::nullopt;
    f.seekg(0, std::ios::beg);
    std::string data(static_cast<std::size_t>(size), '\0');
    if (!f.read(data.data(), size)) return std::nullopt;
    return data;
}

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
    func.body = cloneAst(bodyBlock);
    state_.addFunction(func);

    node->params = params;
    node->body = bodyBlock;
    parent->children.push_back(node);
}

// ---- parseInclude ----
void ParserImpl::parseInclude(AstNode* parent) {
    if (++ParserImpl::includeDepth > MAX_INCLUDE_DEPTH) {
        error("Include depth exceeds 50", true);
        --ParserImpl::includeDepth;
        return;
    }

    auto node = makeStringLiteral("");
    node->type = AstNode::INCLUDE;
    auto pathNode = parseValue();
    node->children.push_back(pathNode);
    if (cur_.type == TOK_NEWLINE) advance();

    std::string rawPath = astNodeToStringWithState(pathNode, &state_);
    if (rawPath.empty()) {
        error("Include path is empty", true);
        --ParserImpl::includeDepth;
        return;
    }

    std::filesystem::path curPath(filename_);
    std::string absPath = (curPath.parent_path() / rawPath).lexically_normal().string();
    std::string canonical = normalizePath(absPath);

    checkIncludeCycle(canonical);
    processingStack_.insert(canonical);

    auto data = read_include_file(canonical);
    if (!data) {
        error(std::format("Cannot read include file: {} (resolved: {})",
                          rawPath, canonical), true);
        processingStack_.erase(canonical);
        --ParserImpl::includeDepth;
        return;
    }

    ParserImpl subParser(*data, canonical, config_, platform_);
    subParser.processingStack_ = processingStack_;
    auto subState = subParser.parse();
    if (subState) {
        for (const auto& [name, rule] : subState->rules()) state_.addRule(rule);
        for (const auto& build : subState->builds()) state_.addBuild(build);
        for (const auto& [name, cfg] : subState->configs()) state_.addConfig(cfg);
        for (const auto& [name, plat] : subState->platforms()) state_.addPlatform(plat);

        for (const auto& [name, func] : subState->functions()) {
            core::FunctionDef copy = func;
            copy.body = cloneAst(func.body);
            state_.addFunction(copy);
        }

        for (const auto& [key, val] : subState->globals()) {
            state_.setGlobal(key, val);
        }
        state_.defaultTargets().insert(state_.defaultTargets().end(),
            subState->defaultTargets().begin(), subState->defaultTargets().end());
        state_.includedFiles().push_back(canonical);
    } else {
        error(std::format("Failed to parse include: {}", rawPath), false);
    }

    processingStack_.erase(canonical);
    --ParserImpl::includeDepth;
    parent->children.push_back(node);
}

// ---- parseImport ----
void ParserImpl::parseImport(AstNode* parent) {
    if (++ParserImpl::includeDepth > MAX_INCLUDE_DEPTH) {
        error("Import depth exceeds 50", true);
        --ParserImpl::includeDepth;
        return;
    }

    auto node = makeStringLiteral("");
    node->type = AstNode::IMPORT;
    node->line = cur_.line;
    node->column = cur_.column;

    auto pathNode = parseValue();
    node->children.push_back(pathNode);
    if (cur_.type == TOK_NEWLINE) advance();

    std::string rawPath = astNodeToStringWithState(pathNode, &state_);
    if (rawPath.empty()) {
        error("Import path is empty", true);
        --ParserImpl::includeDepth;
        return;
    }

    std::filesystem::path curPath(filename_);
    std::string absPath = (curPath.parent_path() / rawPath).lexically_normal().string();
    std::string canonical = normalizePath(absPath);

    checkIncludeCycle(canonical);
    processingStack_.insert(canonical);

    auto data = read_include_file(canonical);
    if (!data) {
        error(std::format("Cannot read import file: {} (resolved: {})",
                          rawPath, canonical), true);
        processingStack_.erase(canonical);
        --ParserImpl::includeDepth;
        return;
    }

    ParserImpl subParser(*data, canonical, config_, platform_);
    subParser.processingStack_ = processingStack_;
    auto subState = subParser.parse();
    if (subState) {
        for (const auto& [name, rule] : subState->rules()) state_.addRule(rule);
        for (const auto& build : subState->builds()) state_.addBuild(build);
        for (const auto& [name, cfg] : subState->configs()) state_.addConfig(cfg);
        for (const auto& [name, plat] : subState->platforms()) state_.addPlatform(plat);

        for (const auto& [name, func] : subState->functions()) {
            core::FunctionDef copy = func;
            copy.body = cloneAst(func.body);
            state_.addFunction(copy);
        }

        for (const auto& [key, val] : subState->globals()) {
            state_.setGlobal(key, val);
        }
        state_.defaultTargets().insert(state_.defaultTargets().end(),
            subState->defaultTargets().begin(), subState->defaultTargets().end());
        state_.includedFiles().push_back(canonical);
    } else {
        error(std::format("Failed to parse import: {}", rawPath), false);
    }

    processingStack_.erase(canonical);
    --ParserImpl::includeDepth;
    parent->children.push_back(node);
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