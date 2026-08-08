#include "parser/parser_impl.h"
#include <QSet>
#include <QFileInfo>

namespace pmake::parser {

// ---- parseStatement ----
void ParserImpl::parseStatement(int depth) {
    if (depth > MAX_STATEMENT_DEPTH) {
        error("Too deep recursion in parser (max 100)", true);
        return;
    }

    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many operations (max 1000000), aborting", false);
        return;
    }

    if (cur_.type != TOK_IDENT) {
        error("Expected statement (identifier)", true);
        advance();
        return;
    }

    QString word = str(cur_.strId);
    auto parent = currentBlock();

    if (word == "pmake_required_version") {
        if (!versionDeclared_) {
            advance();
            parseVersionDirective();
        } else {
            error("pmake_required_version may only be declared once.", true);
            while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
            if (cur_.type == TOK_NEWLINE) advance();
        }
        return;
    }

    if (word == "pmake") {
        error("'pmake' is deprecated. Use 'pmake_required_version = 1.0'.", true);
        advance();
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    if (word == "return") {
        advance();
        AstNode* expr = nullptr;
        if (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF && cur_.type != TOK_INDENT) {
            expr = parseValueSequence(true);
        } else {
            expr = makeStringLiteral("");
        }
        auto node = makeStringLiteral("");
        node->type = AstNode::RETURN;
        node->children.append(expr);
        parent->children.append(node);
        skipNewlinesAndIndents();
        return;
    }

    if (word == "config") {
        advance();
        if (cur_.type == TOK_EQUALS) {
            advance();
            auto val = parseValueSequence(true);
            auto node = makeStringLiteral("");
            node->type = AstNode::ASSIGNMENT;
            node->name = "config";
            node->children.append(val);
            parent->children.append(node);
            skipNewlinesAndIndents();
            return;
        } else {
            parseConfigBlock(parent);
            return;
        }
    }

    if (word == "clean") {
        advance();
        if (cur_.type != TOK_EQUALS) {
            error("Expected '=' after clean", true);
            while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
            if (cur_.type == TOK_NEWLINE) advance();
            return;
        }
        advance();
        auto target = parseValueSequence(true);
        auto node = makeStringLiteral("");
        node->type = AstNode::ALIAS;
        node->name = "clean";
        node->children.append(target);
        parent->children.append(node);
        skipNewlinesAndIndents();
        return;
    }

    if (word == "rule") {
        advance();
        parseRule(parent);
        return;
    }

    if (word == "build") {
        advance();
        parseBuild(parent);
        return;
    }

    if (word == "platform") {
        advance();
        parsePlatform(parent);
        return;
    }

    if (word == "if") {
        advance();
        parseIf(parent);
        return;
    }

    if (word == "foreach") {
        advance();
        parseForeach(parent);
        return;
    }

    if (word == "function") {
        advance();
        parseFunction(parent);
        return;
    }

    if (word == "alias") {
        advance();
        parseAlias(parent);
        return;
    }

    if (word == "include") {
        advance();
        parseInclude(parent);
        return;
    }

    if (word == "switch") {
        advance();
        parseSwitch(parent);
        return;
    }

    if (word == "while") {
        advance();
        parseWhile(parent);
        return;
    }

    if (word == "break") {
        parseBreak(parent);
        return;
    }

    if (word == "continue") {
        parseContinue(parent);
        return;
    }

    if (word == "import") {
        advance();
        parseImport(parent);
        return;
    }

    if (word == "namespace") {
        advance();
        parseNamespace(parent);
        return;
    }

    if (word == "const") {
        advance();
        parseConst(parent);
        return;
    }

    if (word == "enum") {
        advance();
        parseEnum(parent);
        return;
    }

    parseAssignment(parent);
}

// ---- parseAssignment ----
void ParserImpl::parseAssignment(AstNode* parent) {
    if (cur_.type != TOK_IDENT) {
        error("Expected identifier in assignment", true);
        advance();
        return;
    }

    QString name = parseIdent();

    bool isAppend = false;
    if (cur_.type == TOK_EQUALS) {
        isAppend = false;
        advance();
    } else if (cur_.type == TOK_PLUS_EQUALS) {
        isAppend = true;
        advance();
    } else {
        error("Expected '=' or '+='", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    auto valNode = parseValueSequence(true);
    auto node = makeStringLiteral("");
    node->type = AstNode::ASSIGNMENT;
    node->name = name;
    node->isAppend = isAppend;
    node->children.append(valNode);
    node->line = cur_.line;
    node->column = cur_.column;
    skipNewlinesAndIndents();
    parent->children.append(node);
}

// ---- parseVersionDirective ----
void ParserImpl::parseVersionDirective() {
    if (versionDeclared_) {
        error("pmake_required_version may only be declared once.", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (cur_.type != TOK_EQUALS) {
        error("Expected '=' after pmake_required_version", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    advance();
    if (cur_.type != TOK_NUMBER) {
        error("Expected version number (e.g., 1.1)", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    fileVersion_ = cur_.doubleVal;
    advance();
    double currentVersion = getPmakeVersion();
    if (fileVersion_ > currentVersion) {
        error(QString("This project requires PMake %1. Current parser supports PMake %2.")
              .arg(fileVersion_).arg(currentVersion), false);
        return;
    }
    versionDeclared_ = true;
    versionChecked_ = true;
}

} // namespace pmake::parser