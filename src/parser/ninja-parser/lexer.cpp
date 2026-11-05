// parser/ninja-parser/lexer.cpp
#include "parser/ninja-parser/lexer.h"

#include <cctype>
#include <iostream>
#include <print>
#include <string>
#include <string_view>

namespace pmake::ninja {

Lexer::Lexer(std::string_view data, std::string_view filename)
    : data_(data.data()), pos_(data.data()), end_(data.data() + data.size()),
      line_(1), filename_(filename), atLineStart_(true), hadWhitespace_(false) {
    pool_ = &util::StringPool::instance();
}

char Lexer::peek() const {
    return (pos_ < end_) ? *pos_ : '\0';
}

char Lexer::get() {
    if (pos_ >= end_) return '\0';
    char c = *pos_++;
    if (c == '\n') {
        ++line_;
        atLineStart_ = true;
    } else if (c == '\r') {
        if (pos_ < end_ && *pos_ == '\n') {
            ++line_;
            atLineStart_ = true;
            pos_++;
        } else {
            ++line_;
            atLineStart_ = true;
        }
    } else {
        if (c != ' ' && c != '\t') atLineStart_ = false;
    }
    return c;
}

void Lexer::skipWhitespaceAndComments() {
    bool whitespaceSeen = false;
    while (true) {
        char c = peek();
        if (c == '\0') break;
        if (c == ' ' || c == '\t') { get(); whitespaceSeen = true; continue; }
        if (c == '\n' || c == '\r') break;
        if (c == '#') {
            while (peek() != '\0' && peek() != '\n' && peek() != '\r') get();
            continue;
        }
        break;
    }
    if (whitespaceSeen) hadWhitespace_ = true;
}

Token Lexer::readIdentOrKeyword() {
    const char* start = pos_;
    while (true) {
        char n = peek();
        if (n == '\0' || n == ' ' || n == '\t' || n == '\n' || n == '\r' ||
            n == ':' || n == '|' || n == '=' || n == '{' || n == '}' ||
            n == '#' || n == '"' || n == '$') {
            break;
        }
        get();
    }
    std::string_view name(start, static_cast<std::size_t>(pos_ - start));
    int id = pool_->intern(name);
    return {TOK_IDENT, id, line_, 0};
}

Token Lexer::readString() {
    std::string result;
    char c;
    while ((c = get()) != '\0') {
        if (c == '"') break;
        result.push_back(c);
    }
    int id = pool_->intern(result);
    return {TOK_STRING, id, line_, 0};
}

Token Lexer::readVariable() {
    char c = get();
    std::string var;
    if (c == '{') {
        while (true) {
            c = get();
            if (c == '}') break;
            if (c == '\0') { error("Unterminated ${}"); break; }
            var.push_back(c);
        }
        int id = pool_->intern(var);
        return {TOK_VAR_REF, id, line_, 0};
    } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        var.push_back(c);
        while (true) {
            char n = peek();
            if (std::isalnum(static_cast<unsigned char>(n)) || n == '_') var.push_back(get());
            else break;
        }
        int id = pool_->intern(var);
        return {TOK_VAR_REF, id, line_, 0};
    } else {
        // $:  $$  $  — экранирование одиночного символа
        std::string lit(1, c);
        int id = pool_->intern(lit);
        return {TOK_STRING, id, line_, 0};
    }
}

Token Lexer::nextToken() {
    hadWhitespace_ = false;

    // BOM
    if (pos_ == data_ && end_ - pos_ >= 3) {
        if (static_cast<unsigned char>(pos_[0]) == 0xEF &&
            static_cast<unsigned char>(pos_[1]) == 0xBB &&
            static_cast<unsigned char>(pos_[2]) == 0xBF) {
            pos_ += 3;
        }
    }

    if (atLineStart_) {
        int indent = 0;
        while (true) {
            char c = peek();
            if (c == ' ') { get(); indent++; }
            else if (c == '\t') { get(); indent += 4; }
            else break;
        }
        if (indent > 0) { atLineStart_ = false; return {TOK_INDENT, -1, line_, indent}; }
        atLineStart_ = false;
    }

    skipWhitespaceAndComments();

    char c = get();
    if (c == '\0') return {TOK_EOF, -1, line_, 0};
    if (c == '\n' || c == '\r') { atLineStart_ = true; return {TOK_NEWLINE, -1, line_, 0}; }
    if (c == '=') return {TOK_EQUALS, -1, line_, 0};
    if (c == ':') return {TOK_COLON, -1, line_, 0};
    if (c == '{') return {TOK_LBRACE, -1, line_, 0};
    if (c == '}') return {TOK_RBRACE, -1, line_, 0};
    if (c == '|') {
        if (peek() == '|') { get(); return {TOK_PIPE2, -1, line_, 0}; }
        return {TOK_PIPE, -1, line_, 0};
    }
    if (c == '"') return readString();
    if (c == '$') return readVariable();

    --pos_;
    return readIdentOrKeyword();
}

// ============================================================
// readValue корректно обрабатывает ninja-эскейпы:
//   $<newline>  → ничего (continuation)
//   $<space>    → пробел
//   $:          → :
//   $$          → $
//   $var, ${var} → оставить как есть (раскроется позже в expandVars)
//   \X          → \X (literal, в ninja \ не escape)
// ============================================================
std::string Lexer::readValue(bool /*allowContinuation*/) {
    while (pos_ < end_ && (*pos_ == ' ' || *pos_ == '\t')) pos_++;

    std::string result;
    bool inQuotes = false;

    while (pos_ < end_) {
        char c = *pos_;

        if (!inQuotes) {
            if (c == '#' || c == '\n' || c == '\r') break;
        }

        if (c == '"') { inQuotes = !inQuotes; pos_++; continue; }

        if (c == '$' && pos_ + 1 < end_) {
            char n = *(pos_ + 1);

            // Line continuation: $ + newline
            if (n == '\n' || n == '\r') {
                get();                  // consume $
                get();                  // consume \n (или \r\n — get() обрабатывает)
                while (pos_ < end_ && (*pos_ == ' ' || *pos_ == '\t')) pos_++;
                continue;
            }

            if (n == ' ') { result.push_back(' '); pos_ += 2; continue; }
            if (n == ':') { result.push_back(':'); pos_ += 2; continue; }
            if (n == '$') { result.push_back('$'); pos_ += 2; continue; }

            // $var или ${var} — оставить как есть (раскроется позже)
            result.push_back('$');
            pos_++;
            continue;
        }

        result.push_back(c);
        pos_++;
    }

    while (!result.empty() && (result.back() == ' ' || result.back() == '\t'))
        result.pop_back();

    return result;
}

void Lexer::error(std::string_view msg) {
    std::println(std::cerr, "Ninja lexer error in {} line {}: {}", filename_, line_, msg);
}

} // namespace pmake::ninja