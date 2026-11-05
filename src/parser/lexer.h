#pragma once

#include <string_view>
#include "util/string_pool.h"

namespace pmake::parser {

enum TokenType {
    TOK_EOF,
    TOK_NEWLINE,
    TOK_INDENT,
    TOK_EQUALS,
    TOK_PLUS_EQUALS,
    TOK_COLON,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_EQ,
    TOK_NEQ,
    TOK_LE,
    TOK_GE,
    TOK_LT,
    TOK_GT,
    TOK_AND,
    TOK_OR,        // || (логическое OR или order-only в build)
    TOK_PIPE,      // NEW: | (implicit в build)
    TOK_NOT,
    TOK_IDENT,
    TOK_STRING,
    TOK_VAR_REF,
    TOK_NUMBER,
    TOK_TRUE,
    TOK_FALSE,
    TOK_INVALID
};

struct Token {
    TokenType type;
    int strId;
    int line;
    int column;
    int indent;
    union {
        int intVal;
        double doubleVal;
        bool boolVal;
    };
};

class Lexer {
public:
    // ВАЖНО: data и filename должны жить дольше Lexer (string_view не владеет).
    Lexer(std::string_view data, std::string_view filename);

    Token nextToken();
    int line() const { return line_; }
    int column() const { return column_; }
    std::string_view filename() const { return filename_; }

    // Возвращает интернированную строку по id (пустую для невалидного id).
    std::string_view str(int id) const;

private:
    const char* data_;
    const char* pos_;
    const char* end_;
    int line_;
    int column_;
    std::string_view filename_;
    util::StringPool* pool_;
    bool atLineStart_;

    char peek() const;
    char peekNext() const;
    char get();
    void skipWhitespaceAndComments();
    void skipControlChars();
    Token readIdentOrKeyword();
    Token readString();
    Token readNumber();
    Token readVarRef();
    Token readSymbol();
    [[noreturn]] void error(std::string_view msg);
};

} // namespace pmake::parser