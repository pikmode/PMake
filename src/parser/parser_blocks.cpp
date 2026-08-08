#include "parser/parser_impl.h"
#include <QSet>
#include <QFileInfo>

namespace pmake::parser {

// ---- parseBlockDirect ----
void ParserImpl::parseBlockDirect(AstNode* block) {
    if (cur_.type == TOK_LBRACE) {
        advance(); // съедаем {
        skipNewlinesAndIndents(); // пропускаем начальные пробелы/переносы
        while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
            if (cur_.type == TOK_NEWLINE || cur_.type == TOK_INDENT) {
                skipNewlinesAndIndents();
                continue;
            }
            parseStatement(0);
            skipNewlinesAndIndents();
        }
        expect(TOK_RBRACE, "Expected '}'");
    } else if (cur_.type == TOK_INDENT) {
        int baseIndent = cur_.indent;
        advance();
        skipNewlines();
        while (true) {
            if (cur_.type == TOK_EOF) break;
            if (cur_.type == TOK_IDENT) {
                QString word = str(cur_.strId);
                if (word == "endforeach" || word == "endwhile" || word == "endif" ||
                    word == "endfunction" || word == "endnamespace" || word == "endswitch" ||
                    word == "endconfig" || word == "endplatform") {
                    break;
                }
            }
            if (cur_.type == TOK_INDENT && cur_.indent <= baseIndent) {
                break;
            }
            if (cur_.type == TOK_NEWLINE) {
                advance();
                continue;
            }
            if (cur_.type == TOK_INDENT && cur_.indent > baseIndent) {
                advance();
                continue;
            }
            if (cur_.type == TOK_IDENT) {
                parseStatement(0);
                skipNewlinesAndIndents();
            } else {
                error("Unexpected token in block", true);
                break;
            }
        }
    } else {
        error("Expected indentation or '{' for block", true);
    }
}

// ---- parseAttributeBlock ----
AstNode* ParserImpl::parseAttributeBlock() {
    auto block = makeStringLiteral("");
    block->type = AstNode::BLOCK;

    if (cur_.type == TOK_LBRACE) {
        advance();
        skipNewlinesAndIndents();
        int guard = 0;
        while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
            if (++guard > 10000) {
                error("Infinite loop in attribute block (brace)", true);
                break;
            }
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
                auto assign = makeStringLiteral("");
                assign->type = AstNode::ASSIGNMENT;
                assign->name = key;
                assign->isAppend = isAppend;
                assign->children.append(val);
                block->children.append(assign);
                skipNewlinesAndIndents();
            } else {
                break;
            }
        }
        expect(TOK_RBRACE, "Expected '}'");
        if (cur_.type == TOK_NEWLINE) advance();
    } else {
        int baseIndent = cur_.indent;
        if (!skipToBlockStart(0)) {
            error("Expected block", true);
            return block;
        }
        int guard = 0;
        while (true) {
            if (++guard > 10000) {
                error("Infinite loop in attribute block (indent)", true);
                break;
            }
            if (cur_.type == TOK_EOF) break;
            if (cur_.type == TOK_INDENT && cur_.indent == baseIndent) {
                advance();
                break;
            }
            if (cur_.type == TOK_INDENT && cur_.indent < baseIndent) {
                break;
            }
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
                auto assign = makeStringLiteral("");
                assign->type = AstNode::ASSIGNMENT;
                assign->name = key;
                assign->isAppend = isAppend;
                assign->children.append(val);
                block->children.append(assign);
                skipNewlinesAndIndents();
            } else {
                break;
            }
        }
    }
    return block;
}

} // namespace pmake::parser