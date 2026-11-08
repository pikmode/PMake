// parser/ninja-parser/lexer.h
#pragma once

#include <string>
#include <string_view>
#include "util/string_pool.h"

namespace pmake::ninja {

enum TokenType {
    TOK_EOF, TOK_NEWLINE, TOK_INDENT, TOK_EQUALS, TOK_COLON,
    TOK_PIPE, TOK_PIPE2, TOK_LBRACE, TOK_RBRACE,
    TOK_IDENT, TOK_VAR_REF, TOK_STRING,
};

struct Token {
    TokenType type;
    int strId;
    int line;
    int indent;
};

class Lexer {
public:
    // ВАЖНО: filename должен жить дольше Lexer (копируется внутрь — безопасно).
    Lexer(std::string_view data, std::string_view filename);

    Token nextToken();
    std::string readValue(bool allowContinuation = true);
    int line() const { return line_; }
    std::string_view filename() const { return filename_; }
    bool hadWhitespace() const { return hadWhitespace_; }

    // PERF: save/restore для fast-path в parseName (peek-ahead на 1 токен).
    struct LexerState {
        const char* pos;
        int line;
        bool atLineStart;
        bool hadWhitespace;
    };
    LexerState saveState() const {
        return {pos_, line_, atLineStart_, hadWhitespace_};
    }
    void restoreState(const LexerState& s) {
        pos_ = s.pos;
        line_ = s.line;
        atLineStart_ = s.atLineStart;
        hadWhitespace_ = s.hadWhitespace;
    }

private:
    const char* data_;
    const char* pos_;
    const char* end_;
    int line_;
    std::string filename_;
    util::StringPool* pool_;
    bool atLineStart_;
    bool hadWhitespace_ = false;

    char peek() const;
    char get();
    void skipWhitespaceAndComments();
    Token readIdentOrKeyword();
    Token readString();
    Token readVariable();
    void error(std::string_view msg);
};

} // namespace pmake::ninja