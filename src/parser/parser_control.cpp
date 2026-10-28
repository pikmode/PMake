#include "parser/parser_impl.h"
#include <QSet>
#include <QFileInfo>

namespace pmake::parser {

// ---- parseIf ----
void ParserImpl::parseIf(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::IF;
    node->line = cur_.line;
    node->column = cur_.column;

    node->children.append(parseExpressionPrecedence(0, 0));

    if (cur_.type != TOK_NEWLINE && cur_.type != TOK_INDENT && cur_.type != TOK_LBRACE) {
        error("Expected newline or block after if condition", true);
    }
    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block after if", true);
        return;
    }

    auto thenBlock = makeStringLiteral("");
    thenBlock->type = AstNode::BLOCK;
    pushBlock(thenBlock);
    parseBlockDirect(thenBlock);
    popBlock();
    node->children.append(thenBlock);

    int guard = 0;
    while (true) {
        if (++guard > 10000) {
            error("Infinite loop in if/elseif/else", true);
            break;
        }
        skipNewlinesAndIndents();
        if (cur_.type == TOK_IDENT && str(cur_.strId) == "elseif") {
            advance();
            auto elseifNode = makeStringLiteral("");
            elseifNode->type = AstNode::ELSEIF;
            elseifNode->children.append(parseExpressionPrecedence(0, 0));
            if (cur_.type == TOK_NEWLINE) advance();
            skipNewlines();
            if (!skipToBlockStart(0)) {
                error("Expected block after elseif", true);
                break;
            }
            auto elseIfBlock = makeStringLiteral("");
            elseIfBlock->type = AstNode::BLOCK;
            pushBlock(elseIfBlock);
            parseBlockDirect(elseIfBlock);
            popBlock();
            elseifNode->children.append(elseIfBlock);
            node->children.append(elseifNode);
        } else if (cur_.type == TOK_IDENT && str(cur_.strId) == "else") {
            advance();
            auto elseNode = makeStringLiteral("");
            elseNode->type = AstNode::ELSE;
            if (cur_.type == TOK_NEWLINE) advance();
            skipNewlines();
            if (!skipToBlockStart(0)) {
                error("Expected block after else", true);
                break;
            }
            auto elseBlock = makeStringLiteral("");
            elseBlock->type = AstNode::BLOCK;
            pushBlock(elseBlock);
            parseBlockDirect(elseBlock);
            popBlock();
            elseNode->children.append(elseBlock);
            node->children.append(elseNode);
            break;
        } else {
            break;
        }
    }

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endif") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseForeach ----
void ParserImpl::parseForeach(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::FOREACH;
    node->line = cur_.line;
    node->column = cur_.column;

    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    if (cur_.type != TOK_IDENT || str(cur_.strId) != "in") {
        error("Expected 'in' after foreach variable", true);
        return;
    }
    advance();

    node->children.append(parseExpressionPrecedence(0, 0));

    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();

    if (!skipToBlockStart(0)) {
        error("Expected block after foreach", true);
        return;
    }

    auto bodyBlock = makeStringLiteral("");
    bodyBlock->type = AstNode::BLOCK;
    pushBlock(bodyBlock);
    parseBlockDirect(bodyBlock);
    popBlock();

    node->children.append(bodyBlock);

    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseWhile ----
void ParserImpl::parseWhile(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::WHILE;
    node->line = cur_.line;
    node->column = cur_.column;

    node->children.append(parseExpressionPrecedence(0, 0));

    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block after while", true);
        return;
    }

    auto bodyBlock = makeStringLiteral("");
    bodyBlock->type = AstNode::BLOCK;
    pushBlock(bodyBlock);
    parseBlockDirect(bodyBlock);
    popBlock();
    node->children.append(bodyBlock);

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endwhile") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseSwitch ----
void ParserImpl::parseSwitch(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::SWITCH;
    node->line = cur_.line;
    node->column = cur_.column;

    node->children.append(parseExpressionPrecedence(0, 0));

    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block after switch", true);
        return;
    }

    auto casesBlock = makeStringLiteral("");
    casesBlock->type = AstNode::BLOCK;
    pushBlock(casesBlock);
    parseBlockDirect(casesBlock);
    popBlock();

    for (auto child : casesBlock->children) {
        if (child->type != AstNode::CASE && child->type != AstNode::DEFAULT) {
            error("Only 'case' and 'default' allowed inside switch", true);
        }
    }
    node->children.append(casesBlock);

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endswitch") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseCase ----
void ParserImpl::parseCase(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::CASE;
    node->line = cur_.line;
    node->column = cur_.column;

    // Значение case
    node->children.append(parseExpressionPrecedence(0, 0));

    // Блок { ... } или indent
    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();

    if (cur_.type == TOK_LBRACE || cur_.type == TOK_INDENT) {
        auto block = makeStringLiteral("");
        block->type = AstNode::BLOCK;
        pushBlock(block);
        parseBlockDirect(block);
        popBlock();
        node->children.append(block);
    }

    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseDefault ----
void ParserImpl::parseDefault(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::DEFAULT;
    node->line = cur_.line;
    node->column = cur_.column;

    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();

    if (cur_.type == TOK_LBRACE || cur_.type == TOK_INDENT) {
        auto block = makeStringLiteral("");
        block->type = AstNode::BLOCK;
        pushBlock(block);
        parseBlockDirect(block);
        popBlock();
        node->children.append(block);
    }

    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseBreak ----
void ParserImpl::parseBreak(AstNode* parent) {
    advance();
    auto node = makeStringLiteral("");
    node->type = AstNode::BREAK;
    node->line = cur_.line;
    node->column = cur_.column;
    parent->children.append(node);
    if (cur_.type == TOK_NEWLINE) advance();
}

// ---- parseContinue ----
void ParserImpl::parseContinue(AstNode* parent) {
    advance();
    auto node = makeStringLiteral("");
    node->type = AstNode::CONTINUE;
    node->line = cur_.line;
    node->column = cur_.column;
    parent->children.append(node);
    if (cur_.type == TOK_NEWLINE) advance();
}

} // namespace pmake::parser