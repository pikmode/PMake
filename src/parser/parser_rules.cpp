#include "parser/parser_impl.h"
#include <QSet>
#include <QFileInfo>

namespace pmake::parser {

// ---- parseRule ----
void ParserImpl::parseRule(AstNode* parent) {
    if (state_.rules().size() >= MAX_RULES) {
        error("Too many rules (max 10000)", true);
        return;
    }

    QString name = parseIdent();
    if (name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    auto node = makeStringLiteral("");
    node->type = AstNode::RULE;
    node->name = name;
    node->line = cur_.line;
    node->column = cur_.column;

    skipNewlines();
    auto block = parseAttributeBlock();
    node->children = block->children;
    block->children.clear();
    delete block;

    skipNewlinesAndIndents();

    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseBuild ----
void ParserImpl::parseBuild(AstNode* parent) {
    if (state_.builds().size() >= MAX_BUILDS) {
        error("Too many builds (max 10000)", true);
        return;
    }

    auto node = makeStringLiteral("");
    node->type = AstNode::BUILD;
    node->line = cur_.line;
    node->column = cur_.column;

    QVector<AstNode*> outputs;
    int guard = 0;
    while (true) {
        if (++guard > 100000) { // FIX: было 1000
            error("Infinite loop in build outputs", true);
            break;
        }
        if (cur_.type == TOK_STRING || cur_.type == TOK_VAR_REF) {
            outputs.append(parseAtom());
        } else if (cur_.type == TOK_IDENT) {
            error(QString("Expected string literal in quotes, got identifier '%1'. Use \"%1\".").arg(str(cur_.strId)), true);
            advance();
            continue;
        } else {
            break;
        }
        if (cur_.type == TOK_COMMA) advance();
        else break;
    }
    if (outputs.isEmpty()) {
        error("Expected at least one output for build", true);
        return;
    }
    if (outputs.size() > 100000) { // FIX: было 1000
        error("Too many outputs in build (max 100000)", true);
        return;
    }

    for (auto out : outputs) {
        if (out->type == AstNode::STRING_LITERAL) {
            QString outName = out->value.toString();
            if (!outName.isEmpty() && !outName.contains("${")) {
                if (definedBuildOutputs_.contains(outName)) {
                    error(QString("Build output '%1' already defined.").arg(outName), true);
                } else {
                    addDefinition(outName, definedBuildOutputs_);
                }
            }
        }
    }

    if (cur_.type != TOK_COLON) {
        error("Expected ':' after outputs", true);
        return;
    }
    advance();

    if (cur_.type != TOK_IDENT) {
        error("Expected rule name (identifier)", true);
        return;
    }
    QString ruleName = str(cur_.strId);
    node->name = ruleName;
    advance();

    QVector<AstNode*> inputs;
    QVector<AstNode*> implicitInputs;
    QVector<AstNode*> orderOnlyInputs;
    int section = 0;
    guard = 0;
    while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF && cur_.type != TOK_LBRACE) {
        if (++guard > 100000) { // FIX: было 1000
            error("Infinite loop in build inputs", true);
            break;
        }
        if (cur_.type == TOK_PIPE) {
            if (section >= 1) {
                error("Duplicate '|' in build inputs", true);
            }
            section = 1;
            advance();
            continue;
        }
        if (cur_.type == TOK_OR) {
            if (section >= 2) {
                error("Duplicate '||' in build inputs", true);
            }
            section = 2;
            advance();
            continue;
        }
        if (cur_.type == TOK_STRING || cur_.type == TOK_VAR_REF) {
            AstNode* val = parseAtom();
            if (section == 0)      inputs.append(val);
            else if (section == 1) implicitInputs.append(val);
            else                   orderOnlyInputs.append(val);
        } else if (cur_.type == TOK_IDENT) {
            error(QString("Expected string literal in quotes, got identifier '%1'. Use \"%1\".").arg(str(cur_.strId)), true);
            advance();
            continue;
        } else {
            break;
        }
        if (cur_.type == TOK_NEWLINE || cur_.type == TOK_EOF || cur_.type == TOK_LBRACE) break;
    }

    QHash<QString, QString> vars;
    if (cur_.type == TOK_LBRACE) {
        advance();
        skipNewlinesAndIndents();
        while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
            if (cur_.type == TOK_IDENT) {
                QString key = parseIdent();
                bool isAppend = false;
                if (cur_.type == TOK_EQUALS) {
                    isAppend = false;
                    advance();
                } else if (cur_.type == TOK_PLUS_EQUALS) {
                    isAppend = true;
                    advance();
                } else {
                    error("Expected '=' or '+='", true);
                    break;
                }
                auto val = parseValueSequence(false);
                QString value = astNodeToStringWithState(val, &state_);
                if (isAppend) {
                    QString current = vars.value(key);
                    vars[key] = current.isEmpty() ? value : current + " " + value;
                } else {
                    vars[key] = value;
                }
                node->vars[key] = vars[key];
                skipNewlinesAndIndents();
            } else {
                break;
            }
        }
        expect(TOK_RBRACE, "Expected '}'");
        if (cur_.type == TOK_NEWLINE) advance();
    } else if (cur_.type == TOK_INDENT && cur_.indent > 0) {
        auto block = parseAttributeBlock();
        for (auto child : block->children) {
            if (child->type == AstNode::ASSIGNMENT) {
                QString val = astNodeToStringWithState(child->children[0], &state_);
                vars[child->name] = val;
                node->vars[child->name] = val;
            }
        }
        delete block;
    } else {
        if (cur_.type == TOK_NEWLINE) advance();
    }

    auto outConcat = makeConcat(outputs);
    auto inConcat  = makeConcat(inputs);
    node->children.append(outConcat);
    node->children.append(inConcat);

    if (!implicitInputs.isEmpty()) {
        node->implicitInputs = makeConcat(implicitInputs);
    }
    if (!orderOnlyInputs.isEmpty()) {
        node->orderOnlyInputs = makeConcat(orderOnlyInputs);
    }

    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseConfigBlock ----
void ParserImpl::parseConfigBlock(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::CONFIG;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    node->line = cur_.line;
    node->column = cur_.column;

    skipNewlines();
    auto block = parseAttributeBlock();
    node->children = block->children;
    block->children.clear();
    delete block;

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endconfig") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parsePlatform ----
void ParserImpl::parsePlatform(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::PLATFORM;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    node->line = cur_.line;
    node->column = cur_.column;

    skipNewlines();
    auto block = parseAttributeBlock();
    node->children = block->children;
    block->children.clear();
    delete block;

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endplatform") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseAlias ----
void ParserImpl::parseAlias(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::ALIAS;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(node->name, definedAliases_, "Alias")) return;
    addDefinition(node->name, definedAliases_);

    if (cur_.type != TOK_COLON) {
        error("Expected ':' after alias name", true);
        return;
    }
    advance();

    auto targets = parseValueList();

    if (targets.isEmpty() && cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
        error("No valid targets for alias", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    for (auto target : targets) {
        node->children.append(target);
    }

    if (cur_.type == TOK_NEWLINE) advance();
    parent->children.append(node);
    skipNewlinesAndIndents();
}

} // namespace pmake::parser