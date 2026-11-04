// parser/ninja-parser/lexer.cpp
#include "parser/ninja-parser/lexer.h"
#include <cctype>
#include <QDebug>

namespace pmake::ninja {

Lexer::Lexer(const QByteArray& data, const QString& filename)
    : data_(data.constData()), pos_(data_), end_(data_ + data.size()),
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
    QByteArray name(start, pos_ - start);
    int id = pool_->intern(name);
    return {TOK_IDENT, id, line_, 0};
}

Token Lexer::readString() {
    QByteArray result;
    char c;
    while ((c = get()) != '\0') {
        if (c == '"') break;
        result.append(c);
    }
    int id = pool_->intern(result);
    return {TOK_STRING, id, line_, 0};
}

Token Lexer::readVariable() {
    char c = get();
    QByteArray var;
    if (c == '{') {
        while (true) {
            c = get();
            if (c == '}') break;
            if (c == '\0') { error("Unterminated ${}"); break; }
            var.append(c);
        }
        int id = pool_->intern(var);
        return {TOK_VAR_REF, id, line_, 0};
    } else if (isalpha(c) || c == '_') {
        var.append(c);
        while (true) {
            char n = peek();
            if (isalnum(n) || n == '_') var.append(get());
            else break;
        }
        int id = pool_->intern(var);
        return {TOK_VAR_REF, id, line_, 0};
    } else {
        // $:  $$  $  — экранирование одиночного символа
        QByteArray lit(1, c);
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
// FIX: readValue теперь корректно обрабатывает ninja-эскейпы:
//   $<newline>  → ничего (continuation)
//   $<space>    → пробел
//   $:          → :
//   $$          → $
//   $var, ${var} → оставить как есть (раскроется позже в expandVars)
//   \X          → \X (literal, в ninja \ не escape)
// ============================================================
QString Lexer::readValue(bool /*allowContinuation*/) {
    while (pos_ < end_ && (*pos_ == ' ' || *pos_ == '\t')) pos_++;

    QByteArray result;
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

            if (n == ' ') { result.append(' '); pos_ += 2; continue; }
            if (n == ':') { result.append(':'); pos_ += 2; continue; }
            if (n == '$') { result.append('$'); pos_ += 2; continue; }

            // $var или ${var} — оставить как есть (раскроется позже)
            result.append('$');
            pos_++;
            continue;
        }

        result.append(c);
        pos_++;
    }

    while (!result.isEmpty() && (result.back() == ' ' || result.back() == '\t'))
        result.chop(1);

    return QString::fromUtf8(result);
}

void Lexer::error(const QString& msg) {
    qCritical() << "Ninja lexer error in" << filename_ << "line" << line_ << ":" << msg;
}

} // namespace pmake::ninja