#include "parser/parser_impl.h"
#include <QSet>
#include <QFileInfo>

namespace pmake::parser {

// ---- parseFunction ----
void ParserImpl::parseFunction(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::FUNCTION_DEF;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(node->name, definedFunctions_, "Function")) return;
    addDefinition(node->name, definedFunctions_);

    expect(TOK_LPAREN, "Expected '('");
    QStringList params;
    while (cur_.type != TOK_RPAREN && cur_.type != TOK_EOF) {
        if (cur_.type == TOK_IDENT) {
            params.append(str(cur_.strId));
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
    func.body = bodyBlock;
    state_.addFunction(func);

    node->params = params;
    node->body = bodyBlock;
    parent->children.append(node);
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
    node->children.append(pathNode);
    if (cur_.type == TOK_NEWLINE) advance();

    QString path = astNodeToStringWithState(pathNode, &state_);
    if (path.isEmpty()) {
        error("Include path is empty", true);
        --ParserImpl::includeDepth;
        return;
    }

    QString canonical = normalizePath(path);
    checkIncludeCycle(canonical);
    processingStack_.insert(canonical);

    QByteArray data;
    if (!util::readFile(path, data)) {
        error(QString("Cannot read include file: %1").arg(path), true);
        processingStack_.remove(canonical);
        --ParserImpl::includeDepth;
        return;
    }

    ParserImpl subParser(data, path, config_, platform_);
    auto subState = subParser.parse();
    if (subState) {
        for (const auto& rule : subState->rules()) state_.addRule(rule);
        for (const auto& build : subState->builds()) state_.addBuild(build);
        for (const auto& cfg : subState->configs()) state_.addConfig(cfg);
        for (const auto& plat : subState->platforms()) state_.addPlatform(plat);
        for (const auto& func : subState->functions()) state_.addFunction(func);
        for (const auto& key : subState->globals().keys()) {
            auto val = subState->getGlobal(key);
            if (val.has_value()) state_.setGlobal(key, *val);
        }
        state_.defaultTargets().append(subState->defaultTargets());
        state_.includedFiles().append(path);
    } else {
        error(QString("Failed to parse include: %1").arg(path), true);
    }

    processingStack_.remove(canonical);
    --ParserImpl::includeDepth;
    parent->children.append(node);
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
    node->children.append(pathNode);
    if (cur_.type == TOK_NEWLINE) advance();

    QString path = astNodeToStringWithState(pathNode, &state_);
    if (path.isEmpty()) {
        error("Import path is empty", true);
        --ParserImpl::includeDepth;
        return;
    }

    QString canonical = normalizePath(path);
    checkIncludeCycle(canonical);
    processingStack_.insert(canonical);

    QByteArray data;
    if (!util::readFile(path, data)) {
        error(QString("Cannot read import file: %1").arg(path), true);
        processingStack_.remove(canonical);
        --ParserImpl::includeDepth;
        return;
    }

    ParserImpl subParser(data, path, config_, platform_);
    auto subState = subParser.parse();
    if (subState) {
        for (const auto& rule : subState->rules()) state_.addRule(rule);
        for (const auto& build : subState->builds()) state_.addBuild(build);
        for (const auto& cfg : subState->configs()) state_.addConfig(cfg);
        for (const auto& plat : subState->platforms()) state_.addPlatform(plat);
        for (const auto& func : subState->functions()) state_.addFunction(func);
        for (const auto& key : subState->globals().keys()) {
            auto val = subState->getGlobal(key);
            if (val.has_value()) state_.setGlobal(key, *val);
        }
        state_.defaultTargets().append(subState->defaultTargets());
        state_.includedFiles().append(path);
    } else {
        error(QString("Failed to parse import: %1").arg(path), true);
    }

    processingStack_.remove(canonical);
    --ParserImpl::includeDepth;
    parent->children.append(node);
}

// ---- parseNamespace ----
void ParserImpl::parseNamespace(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::NAMESPACE;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
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
    node->children.append(bodyBlock);

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endnamespace") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseConst ----
void ParserImpl::parseConst(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::CONST_DECL;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
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
    node->children.append(val);
    node->line = cur_.line;
    node->column = cur_.column;
    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseEnum ----
void ParserImpl::parseEnum(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::ENUM_DECL;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
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

    QVector<AstNode*> enumerators;
    while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
        if (cur_.type == TOK_IDENT) {
            QString name = parseIdent();
            auto item = makeStringLiteral(name);
            enumerators.append(item);
            if (cur_.type == TOK_COMMA) advance();
        } else {
            error("Expected enumerator name", true);
            break;
        }
    }
    expect(TOK_RBRACE, "Expected '}'");
    node->children = enumerators;
    node->line = cur_.line;
    node->column = cur_.column;

    if (cur_.type == TOK_NEWLINE) advance();
    parent->children.append(node);
}

} // namespace pmake::parser