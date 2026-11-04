// parser/ninja-parser/lexer.h
#pragma once

#include <QByteArray>
#include <QVector>
#include "util/string_pool.h"

namespace pmake::ninja {   // ← было pmake::parser

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
    Lexer(const QByteArray& data, const QString& filename);
    Token nextToken();
    QString readValue(bool allowContinuation = true);
    int line() const { return line_; }
    const QString& filename() const { return filename_; }
    bool hadWhitespace() const { return hadWhitespace_; }

private:
    const char* data_;
    const char* pos_;
    const char* end_;
    int line_;
    QString filename_;
    util::StringPool* pool_;
    bool atLineStart_;
    bool hadWhitespace_ = false;

    char peek() const;
    char get();
    void skipWhitespaceAndComments();
    Token readIdentOrKeyword();
    Token readString();
    Token readVariable();
    void error(const QString& msg);
};

} // namespace pmake::ninja