#include "parser/lexer.h"

#include <cctype>
#include <charconv>
#include <format>
#include <iostream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pmake::parser {

Lexer::Lexer(std::string_view data, std::string_view filename)
    : data_(data.data()), pos_(data.data()), end_(data.data() + data.size()),
      line_(1), column_(0), filename_(filename), atLineStart_(true) {
    pool_ = &util::StringPool::instance();
    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        pos_ += 3;
        column_ = 3;
    }
}

char Lexer::peek() const {
    return (pos_ < end_) ? *pos_ : '\0';
}

char Lexer::peekNext() const {
    return (pos_ + 1 < end_) ? *(pos_ + 1) : '\0';
}

char Lexer::get() {
    if (pos_ >= end_) return '\0';
    char c = *pos_++;
    if (c == '\n') {
        ++line_;
        column_ = 0;
        atLineStart_ = true;
    } else if (c == '\r') {
        if (pos_ < end_ && *pos_ == '\n') {
            ++line_;
            column_ = 0;
            atLineStart_ = true;
            pos_++;
        } else {
            ++line_;
            column_ = 0;
            atLineStart_ = true;
        }
    } else {
        ++column_;
        if (c != ' ' && c != '\t') atLineStart_ = false;
    }
    return c;
}

void Lexer::skipWhitespaceAndComments() {
    while (true) {
        if (pos_ >= end_) break;
        char c = peek();
        if (c == ' ' || c == '\t') {
            get();
            continue;
        }
        if (c == '/' && pos_ + 1 < end_ && *(pos_ + 1) == '/') {
            get(); get();
            while (pos_ < end_ && peek() != '\n' && peek() != '\r')
                get();
            if (pos_ < end_ && (peek() == '\n' || peek() == '\r')) {
                char n = get();
                if (n == '\r' && peek() == '\n')
                    get();
                atLineStart_ = true;
            }
            continue;
        }
        if (c == '/' && pos_ + 1 < end_ && *(pos_ + 1) == '*') {
            get(); get();
            while (pos_ < end_) {
                if (peek() == '*' && pos_ + 1 < end_ && *(pos_ + 1) == '/') {
                    get(); get();
                    break;
                }
                get();
            }
            continue;
        }
        break;
    }
}

void Lexer::skipControlChars() {
    while (pos_ < end_) {
        char c = peek();
        if ((c >= 0 && c < 32 && c != '\n' && c != '\r' && c != '\t') ||
            static_cast<unsigned char>(c) == 0xA0) {
            get();
        } else {
            break;
        }
    }
}

Token Lexer::readIdentOrKeyword() {
    const char* start = pos_;
    while (pos_ < end_) {
        char n = peek();
        if (n >= 0 && n < 32) break;
        if (std::isalnum(static_cast<unsigned char>(n)) || n == '_' || n == '.' || n == '/' || n == '-' || n == '\\') {
            get();
        } else {
            break;
        }
    }
    std::string_view name(start, static_cast<std::size_t>(pos_ - start));
    int id = pool_->intern(name);

    if (name == "true") {
        Token t{};
        t.type = TOK_TRUE;
        t.strId = -1;
        t.line = line_;
        t.column = column_;
        t.indent = 0;
        t.boolVal = true;
        return t;
    }
    if (name == "false") {
        Token t{};
        t.type = TOK_FALSE;
        t.strId = -1;
        t.line = line_;
        t.column = column_;
        t.indent = 0;
        t.boolVal = false;
        return t;
    }
    return {TOK_IDENT, id, line_, column_, 0};
}

Token Lexer::readString() {
    std::string result;
    bool escaped = false;
    bool foundClosing = false;

    while (pos_ < end_) {
        char c = get();

        if (c == '"' && !escaped) {
            foundClosing = true;
            break;
        }

        if (escaped) {
            switch (c) {
                case 'n':  result.push_back('\n'); break;
                case 'r':  result.push_back('\r'); break;
                case '\\': result.push_back('\\'); break;
                case '"':  result.push_back('"');  break;
                // CHANGED: 't' убран из escape-таблицы.
                // Причина: Windows-пути вида "C:\temp" больше не превращаются
                // в "C:<TAB>emp". Если нужен настоящий TAB — используйте
                // реальный символ табуляции в исходнике.
                default:
                    // Неизвестный escape — сохраняем ОБА символа как есть.
                    // Это защищает "C:\path", "C:\Program Files" и т.п.
                    result.push_back('\\');
                    result.push_back(c);
                    break;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            result.push_back(c);
        }
    }

    if (!foundClosing) {
        error("Unterminated string literal");
    }

    int id = pool_->intern(result);
    return {TOK_STRING, id, line_, column_, 0};
}

Token Lexer::readNumber() {
    const char* start = pos_;
    while (pos_ < end_ && std::isdigit(static_cast<unsigned char>(peek())))
        get();
    if (peek() == '.') {
        get();
        while (pos_ < end_ && std::isdigit(static_cast<unsigned char>(peek())))
            get();
    }
    std::string_view num(start, static_cast<std::size_t>(pos_ - start));
    double d = 0.0;
    auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), d);
    if (ec != std::errc{}) {
        d = 0.0;
    }
    Token t{};
    t.type = TOK_NUMBER;
    t.strId = -1;
    t.line = line_;
    t.column = column_;
    t.indent = 0;
    t.doubleVal = d;
    return t;
}

Token Lexer::readVarRef() {
    std::string var;
    while (pos_ < end_) {
        char c = get();
        if (c == '}') break;
        var.push_back(c);
    }
    if (pos_ >= end_) error("Unterminated variable reference");
    int id = pool_->intern(var);
    return {TOK_VAR_REF, id, line_, column_, 0};
}

Token Lexer::readSymbol() {
    if (pos_ >= end_) return {TOK_EOF, -1, line_, column_, 0};
    char c = get();
    switch (c) {
        case '=':
            if (peek() == '=') { get(); return {TOK_EQ, -1, line_, column_, 0}; }
            return {TOK_EQUALS, -1, line_, column_, 0};
        case '+':
            if (peek() == '=') { get(); return {TOK_PLUS_EQUALS, -1, line_, column_, 0}; }
            return {TOK_PLUS, -1, line_, column_, 0};
        case '!':
            if (peek() == '=') { get(); return {TOK_NEQ, -1, line_, column_, 0}; }
            return {TOK_NOT, -1, line_, column_, 0};
        case '<':
            if (peek() == '=') { get(); return {TOK_LE, -1, line_, column_, 0}; }
            return {TOK_LT, -1, line_, column_, 0};
        case '>':
            if (peek() == '=') { get(); return {TOK_GE, -1, line_, column_, 0}; }
            return {TOK_GT, -1, line_, column_, 0};
        case '&':
            if (peek() == '&') { get(); return {TOK_AND, -1, line_, column_, 0}; }
            return {TOK_INVALID, -1, line_, column_, 0};
        case '|':
            if (peek() == '|') { get(); return {TOK_OR, -1, line_, column_, 0}; }
            return {TOK_PIPE, -1, line_, column_, 0};
        case ':': return {TOK_COLON, -1, line_, column_, 0};
        case '}': return {TOK_RBRACE, -1, line_, column_, 0};
        case '(': return {TOK_LPAREN, -1, line_, column_, 0};
        case ')': return {TOK_RPAREN, -1, line_, column_, 0};
        case '[': return {TOK_LBRACKET, -1, line_, column_, 0};
        case ']': return {TOK_RBRACKET, -1, line_, column_, 0};
        case ',': return {TOK_COMMA, -1, line_, column_, 0};
        case '-': return {TOK_MINUS, -1, line_, column_, 0};
        case '*': return {TOK_STAR, -1, line_, column_, 0};
        case '/': return {TOK_SLASH, -1, line_, column_, 0};
        default:
            return {TOK_INVALID, -1, line_, column_, 0};
    }
}

Token Lexer::nextToken() {
    while (true) {
        skipControlChars();
        if (pos_ >= end_) return {TOK_EOF, -1, line_, column_, 0};

        if (atLineStart_) {
            int indent = 0;
            while (pos_ < end_) {
                char n = peek();
                if (n == ' ') { get(); indent++; }
                else if (n == '\t') { get(); indent += 4; }
                else break;
            }
            if (indent > 0) {
                atLineStart_ = false;
                return {TOK_INDENT, -1, line_, column_, indent};
            }
            atLineStart_ = false;
        }

        skipWhitespaceAndComments();
        if (pos_ >= end_) return {TOK_EOF, -1, line_, column_, 0};

        char c = peek();
        if (c == '\n' || c == '\r') {
            get();
            atLineStart_ = true;
            return {TOK_NEWLINE, -1, line_, column_, 0};
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            return readIdentOrKeyword();

        if (std::isdigit(static_cast<unsigned char>(c))) {
            const char* p = pos_;
            while (p < end_ && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.')) p++;
            if (p < end_ && (std::isalpha(static_cast<unsigned char>(*p)) || *p == '_'))
                return readIdentOrKeyword();
            else
                return readNumber();
        }

        if (c == '"') {
            get();
            return readString();
        }

        if (c == '$' && peekNext() == '{') {
            get(); get();
            return readVarRef();
        }

        if (c == '{') {
            get();
            return {TOK_LBRACE, -1, line_, column_, 0};
        }

        Token t = readSymbol();
        if (t.type == TOK_INVALID) {
            error("Invalid character");
            get();
            continue;
        }
        return t;
    }
}

std::string_view Lexer::str(int id) const {
    if (id < 0 || id >= static_cast<int>(pool_->size())) return {};
    return pool_->get(id);
}

void Lexer::error(std::string_view msg) {
    std::string err = std::format("Lexer error in {} line {}: {}", filename_, line_, msg);
    std::println(std::cerr, "{}", err);
    throw std::runtime_error(std::move(err));
}

} // namespace pmake::parser