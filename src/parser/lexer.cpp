#include "parser/lexer.h"
#include <cctype>
#include <QDebug>
#include <QString>
#include <stdexcept>

namespace pmake::parser {

Lexer::Lexer(const QByteArray& data, const QString& filename)
    : data_(data.constData()), pos_(data_), end_(pos_ + data.size()),
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
        if (isalnum(static_cast<unsigned char>(n)) || n == '_' || n == '.' || n == '/' || n == '-' || n == '\\') {
            get();
        } else {
            break;
        }
    }
    QByteArray name(start, pos_ - start);
    int id = pool_->intern(name);
    QString str = QString::fromUtf8(name);
    if (str == QLatin1String("true"))
        return {TOK_TRUE, -1, line_, column_, 0, .boolVal = true};
    if (str == QLatin1String("false"))
        return {TOK_FALSE, -1, line_, column_, 0, .boolVal = false};
    return {TOK_IDENT, id, line_, column_, 0};
}

Token Lexer::readString() {
    QByteArray result;
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
                case 'n':  result.append('\n'); break;
                case 'r':  result.append('\r'); break;
                case 't':  result.append('\t'); break;
                case '\\': result.append('\\'); break;
                case '"':  result.append('"');  break;
                default:   result.append(c);    break;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            result.append(c);
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
    while (pos_ < end_ && isdigit(static_cast<unsigned char>(peek())))
        get();
    if (peek() == '.') {
        get();
        while (pos_ < end_ && isdigit(static_cast<unsigned char>(peek())))
            get();
    }
    QByteArray num(start, pos_ - start);
    bool ok;
    double d = num.toDouble(&ok);
    if (ok) {
        return {TOK_NUMBER, -1, line_, column_, 0, .doubleVal = d};
    } else {
        int val = num.toInt();
        return {TOK_NUMBER, -1, line_, column_, 0, .doubleVal = static_cast<double>(val)};
    }
}

Token Lexer::readVarRef() {
    QByteArray var;
    while (pos_ < end_) {
        char c = get();
        if (c == '}') break;
        var.append(c);
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
            // FIX: одиночный | теперь TOK_PIPE (для implicit inputs в build).
            // Раньше был TOK_INVALID.
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

        if (isalpha(static_cast<unsigned char>(c)) || c == '_')
            return readIdentOrKeyword();

        if (isdigit(static_cast<unsigned char>(c))) {
            const char* p = pos_;
            while (p < end_ && (isdigit(static_cast<unsigned char>(*p)) || *p == '.')) p++;
            if (p < end_ && (isalpha(static_cast<unsigned char>(*p)) || *p == '_'))
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

QString Lexer::str(int id) const {
    if (id < 0 || id >= pool_->size()) return QString();
    return QString::fromUtf8(pool_->get(id));
}

void Lexer::error(const QString& msg) {
    QString err = QString("Lexer error in %1 line %2: %3").arg(filename_).arg(line_).arg(msg);
    qCritical() << err;
    throw std::runtime_error(err.toStdString());
}

} // namespace pmake::parser