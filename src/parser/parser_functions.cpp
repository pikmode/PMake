
#include "parser/parser_impl.h"
#include <QSet>
#include <QFileInfo>
#include <QDir>

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
    for (AstNode* ch : n->children) c->children.append(cloneAst(ch));
    for (AstNode* a : n->attributes) c->attributes.append(cloneAst(a));
    c->left = cloneAst(n->left);
    c->right = cloneAst(n->right);
    c->body = cloneAst(n->body);
    c->implicitInputs = cloneAst(n->implicitInputs);    // NEW
    c->orderOnlyInputs = cloneAst(n->orderOnlyInputs);  // NEW
    return c;
}

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
    func.body = cloneAst(bodyBlock);   // State получает клон — без double-free
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

    QString rawPath = astNodeToStringWithState(pathNode, &state_);
    if (rawPath.isEmpty()) {
        error("Include path is empty", true);
        --ParserImpl::includeDepth;
        return;
    }

    QFileInfo curInfo(filename_);
    QString absPath = QDir::cleanPath(curInfo.absolutePath() + '/' + rawPath);

    QString canonical = normalizePath(absPath);
    checkIncludeCycle(canonical);   // ← теперь throw при цикле
    processingStack_.insert(canonical);

    QByteArray data;
    if (!util::readFile(canonical, data)) {
        error(QString("Cannot read include file: %1 (resolved: %2)")
              .arg(rawPath).arg(canonical), true);
        processingStack_.remove(canonical);
        --ParserImpl::includeDepth;
        return;
    }

    ParserImpl subParser(data, canonical, config_, platform_);
    subParser.processingStack_ = processingStack_;
    auto subState = subParser.parse();
    if (subState) {
        for (const auto& rule : subState->rules()) state_.addRule(rule);
        for (const auto& build : subState->builds()) state_.addBuild(build);
        for (const auto& cfg : subState->configs()) state_.addConfig(cfg);
        for (const auto& plat : subState->platforms()) state_.addPlatform(plat);

        for (const auto& func : subState->functions()) {
            core::FunctionDef copy = func;
            copy.body = cloneAst(func.body);
            state_.addFunction(copy);
        }

        for (const auto& key : subState->globals().keys()) {
            auto val = subState->getGlobal(key);
            if (val.has_value()) state_.setGlobal(key, *val);
        }
        state_.defaultTargets().append(subState->defaultTargets());
        state_.includedFiles().append(canonical);
    } else {
        // recover=false — прерываем цепочку, чтобы ошибка не игнорировалась
        error(QString("Failed to parse include: %1").arg(rawPath), false);
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

    QString rawPath = astNodeToStringWithState(pathNode, &state_);
    if (rawPath.isEmpty()) {
        error("Import path is empty", true);
        --ParserImpl::includeDepth;
        return;
    }

    QFileInfo curInfo(filename_);
    QString absPath = QDir::cleanPath(curInfo.absolutePath() + '/' + rawPath);

    QString canonical = normalizePath(absPath);
    checkIncludeCycle(canonical);
    processingStack_.insert(canonical);

    QByteArray data;
    if (!util::readFile(canonical, data)) {
        error(QString("Cannot read import file: %1 (resolved: %2)")
              .arg(rawPath).arg(canonical), true);
        processingStack_.remove(canonical);
        --ParserImpl::includeDepth;
        return;
    }

    ParserImpl subParser(data, canonical, config_, platform_);
    subParser.processingStack_ = processingStack_;
    auto subState = subParser.parse();
    if (subState) {
        for (const auto& rule : subState->rules()) state_.addRule(rule);
        for (const auto& build : subState->builds()) state_.addBuild(build);
        for (const auto& cfg : subState->configs()) state_.addConfig(cfg);
        for (const auto& plat : subState->platforms()) state_.addPlatform(plat);

        for (const auto& func : subState->functions()) {
            core::FunctionDef copy = func;
            copy.body = cloneAst(func.body);
            state_.addFunction(copy);
        }

        for (const auto& key : subState->globals().keys()) {
            auto val = subState->getGlobal(key);
            if (val.has_value()) state_.setGlobal(key, *val);
        }
        state_.defaultTargets().append(subState->defaultTargets());
        state_.includedFiles().append(canonical);
    } else {
        error(QString("Failed to parse import: %1").arg(rawPath), false);
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