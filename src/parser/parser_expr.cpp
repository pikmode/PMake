#include "parser/parser_impl.h"

namespace pmake::parser {

int ParserImpl::getPrecedence(TokenType t) {
    switch (t) {
        case TOK_OR:  return 1;
        case TOK_AND: return 2;
        case TOK_EQ: case TOK_NEQ: return 3;
        case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE: return 4;
        case TOK_PLUS: case TOK_MINUS: return 5;
        case TOK_STAR: case TOK_SLASH: return 6;
        default: return -1;
    }
}

AstNode* ParserImpl::parseExpressionPrecedence(int minPrec, int depth) {
    if (depth > MAX_EXPR_DEPTH) {
        error("Expression too deep (max 100), aborting", false);
        return nullptr;
    }

    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many operations in expression parsing", false);
        return nullptr;
    }

    auto left = parseAtom();
    if (!left) return nullptr;

    while (true) {
        int prec = getPrecedence(cur_.type);
        if (prec < minPrec) break;
        TokenType op = cur_.type;
        advance();
        auto right = parseExpressionPrecedence(prec + 1, depth + 1);
        if (!right) {
            error("Missing right operand", true);
            break;
        }
        left = makeBinaryOp(op, left, right);
    }
    return left;
}

AstNode* ParserImpl::parseAtom() {
    if (cur_.type == TOK_IDENT) {
        QString ident = str(cur_.strId);
        advance();
        if (cur_.type == TOK_LPAREN) {
            advance();
            QVector<AstNode*> args;
            int argCount = 0;
            const int MAX_ARGS = 1000;

            while (cur_.type != TOK_RPAREN && cur_.type != TOK_EOF) {
                if (++argCount > MAX_ARGS) {
                    error("Too many arguments in function call", true);
                    break;
                }

                TokenType before = cur_.type;
                AstNode* arg = parseExpressionPrecedence(0, 0);
                if (arg) {
                    args.append(arg);
                } else {
                    error("Failed to parse argument", true);
                    break;
                }

                if (cur_.type == before) {
                    error("Parser stuck in function arguments", true);
                    advance();
                    break;
                }

                if (cur_.type == TOK_COMMA) {
                    advance();
                } else {
                    break;
                }
            }

            expect(TOK_RPAREN, "Expected ')'");
            auto node = makeStringLiteral("");
            node->type = AstNode::FUNCTION_CALL;
            node->name = ident;
            node->children = args;
            node->line = cur_.line;
            node->column = cur_.column;
            return node;
        } else {
            return makeVariableRef(ident);
        }
    } else if (cur_.type == TOK_VAR_REF) {
        QString name = str(cur_.strId);
        advance();
        return makeVariableRef(name);
    } else if (cur_.type == TOK_STRING) {
        QString s = str(cur_.strId);
        advance();
        return makeStringLiteral(s);
    } else if (cur_.type == TOK_NUMBER) {
        double val = cur_.doubleVal;
        advance();
        return makeNumberLiteral(val);
    } else if (cur_.type == TOK_TRUE) {
        advance();
        return makeBoolLiteral(true);
    } else if (cur_.type == TOK_FALSE) {
        advance();
        return makeBoolLiteral(false);
    } else if (cur_.type == TOK_LBRACKET) {
        advance();
        QVector<AstNode*> elements;
        while (cur_.type != TOK_RBRACKET && cur_.type != TOK_EOF) {
            elements.append(parseExpressionPrecedence(0, 0));
            if (cur_.type == TOK_COMMA) advance();
        }
        expect(TOK_RBRACKET, "Expected ']'");
        return makeList(elements);
    } else if (cur_.type == TOK_LPAREN) {
        advance();
        auto expr = parseExpressionPrecedence(0, 0);
        expect(TOK_RPAREN, "Expected ')'");
        return expr;
    } else if (cur_.type == TOK_NOT) {
        advance();
        auto operand = parseExpressionPrecedence(getPrecedence(TOK_NOT), 0);
        return makeUnaryOp(TOK_NOT, operand);
    } else if (cur_.type == TOK_MINUS) {
        advance();
        auto operand = parseExpressionPrecedence(getPrecedence(TOK_MINUS), 0);
        auto node = makeStringLiteral("");
        node->type = AstNode::UNARY_OP;
        node->op = TOK_MINUS;
        node->left = operand;
        node->line = cur_.line;
        node->column = cur_.column;
        return node;
    } else {
        advance();
        return makeStringLiteral("");
    }
}

// ================================================================
// ИСПРАВЛЕННЫЙ parseValueSequence – без ложной проверки на застревание
// ================================================================
AstNode* ParserImpl::parseValueSequence(bool stopOnNewline) {
    QVector<AstNode*> parts;
    int loopGuard = 0;
    while (true) {
        if (++loopGuard > 10000) {
            error("Infinite loop in parseValueSequence", true);
            break;
        }
        if (cur_.type == TOK_NEWLINE || cur_.type == TOK_EOF)
            break;
        if (cur_.type == TOK_RBRACE || cur_.type == TOK_RPAREN || cur_.type == TOK_RBRACKET)
            break;
        if (stopOnNewline && cur_.type == TOK_INDENT)
            break;

        AstNode* part = parseExpressionPrecedence(0, 0);
        if (!part) {
            error("Failed to parse expression in value sequence", true);
            break;
        }
        parts.append(part);

        if (cur_.type == TOK_NEWLINE || cur_.type == TOK_EOF)
            break;
    }
    if (parts.isEmpty())
        return makeStringLiteral("");
    if (parts.size() == 1)
        return parts.first();
    return makeConcat(parts);
}

QVector<AstNode*> ParserImpl::parseValueList() {
    QVector<AstNode*> list;
    int loopGuard = 0;
    while (true) {
        if (++loopGuard > 10000) {
            error("Infinite loop in parseValueList", true);
            break;
        }
        if (cur_.type == TOK_NEWLINE || cur_.type == TOK_EOF ||
            cur_.type == TOK_RBRACE || cur_.type == TOK_RPAREN || cur_.type == TOK_RBRACKET)
            break;
        if (cur_.type == TOK_IDENT || cur_.type == TOK_STRING || cur_.type == TOK_VAR_REF ||
            cur_.type == TOK_LBRACKET || cur_.type == TOK_NUMBER || cur_.type == TOK_TRUE || cur_.type == TOK_FALSE) {
            AstNode* expr = parseExpressionPrecedence(0, 0);
            if (expr) {
                list.append(expr);
            } else {
                error("Failed to parse expression in list", true);
                break;
            }
        } else {
            break;
        }
        if (cur_.type == TOK_COMMA) advance();
        else break;
    }
    return list;
}

AstNode* ParserImpl::parseValue() {
    return parseExpressionPrecedence(0, 0);
}

} // namespace pmake::parser