// parser/lexer.cpp
#include "parser/lexer.h"
#include <cctype>
#include <QDebug>

namespace pmake::parser {

Lexer::Lexer(const QByteArray& data, const QString& filename)
    : data_(data.constData()), pos_(data_), end_(data_ + data.size()), line_(1), filename_(filename), atLineStart_(true), hadWhitespace_(false) {
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
        if (c != ' ' && c != '\t') {
            atLineStart_ = false;
        }
    }
    return c;
}

void Lexer::skipWhitespaceAndComments() {
    bool whitespaceSeen = false;
    while (true) {
        char c = peek();
        if (c == '\0') break;
        if (c == ' ' || c == '\t') {
            get();
            whitespaceSeen = true;
            continue;
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        if (c == '#') {
            // Комментарий до конца строки
            while (peek() != '\0' && peek() != '\n' && peek() != '\r') {
                get();
            }
            continue;
        }
        break;
    }
    if (whitespaceSeen) {
        hadWhitespace_ = true;
    }
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
    const char* start = pos_;
    QByteArray result;
    char c;
    while ((c = get()) != '\0') {
        if (c == '"') break;
        if (c == '\\') {
            char n = peek();
            if (n == '\0') break;
            get();
            switch (n) {
                case 'n':  result.append('\n'); break;
                case 'r':  result.append('\r'); break;
                case 't':  result.append('\t'); break;
                case '\\': result.append('\\'); break;
                case '"':  result.append('"'); break;
                default:   result.append(n); break;
            }
        } else {
            result.append(c);
        }
    }
    int id = pool_->intern(result);
    return {TOK_STRING, id, line_, 0};
}

Token Lexer::readVariable() {
    char c = get(); // символ после $
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
        // Экранирование: $: -> :, $  -> пробел, $$ -> $ и т.д.
        QByteArray lit(1, c);
        int id = pool_->intern(lit);
        return {TOK_STRING, id, line_, 0};
    }
}

Token Lexer::nextToken() {
    hadWhitespace_ = false;

    // Пропускаем BOM
    if (pos_ == data_ && end_ - pos_ >= 3) {
        if (static_cast<unsigned char>(pos_[0]) == 0xEF &&
            static_cast<unsigned char>(pos_[1]) == 0xBB &&
            static_cast<unsigned char>(pos_[2]) == 0xBF) {
            pos_ += 3;
        }
    }

    // Отступы в начале строки
    if (atLineStart_) {
        int indent = 0;
        while (true) {
            char c = peek();
            if (c == ' ') {
                get();
                indent++;
            } else if (c == '\t') {
                get();
                indent += 4;
            } else {
                break;
            }
        }
        if (indent > 0) {
            atLineStart_ = false;
            return {TOK_INDENT, -1, line_, indent};
        }
        atLineStart_ = false;
    }

    skipWhitespaceAndComments();

    char c = get();
    if (c == '\0') return {TOK_EOF, -1, line_, 0};

    if (c == '\n' || c == '\r') {
        atLineStart_ = true;
        return {TOK_NEWLINE, -1, line_, 0};
    }

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

    // Любой другой символ – начало идентификатора
    --pos_;
    return readIdentOrKeyword();
}

// ============================================================
// ИСПРАВЛЕННЫЙ readValue – корректно обрабатывает кавычки и #
// ============================================================
QString Lexer::readValue(bool allowContinuation) {
    // Пропускаем начальные пробелы
    while (pos_ < end_ && (*pos_ == ' ' || *pos_ == '\t')) {
        pos_++;
    }

    QByteArray result;
    bool inQuotes = false;
    bool escape = false;

    while (pos_ < end_) {
        char c = *pos_;
        if (c == '\0') break;

        // Если не в кавычках и не экранируем, обрабатываем специальные символы
        if (!inQuotes && !escape) {
            if (c == '#' || c == '\n' || c == '\r') {
                // # – комментарий (только если не экранирован и не в кавычках)
                if (c == '#') {
                    // До конца строки – пропускаем
                    while (pos_ < end_ && *pos_ != '\n' && *pos_ != '\r') {
                        pos_++;
                    }
                    break;
                }
                // Конец строки – прерываем чтение значения
                if (c == '\n' || c == '\r') {
                    break;
                }
            }
            if (c == '\\') {
                // Экранирование: проверяем следующий символ
                if (pos_ + 1 < end_) {
                    char next = *(pos_ + 1);
                    if (next == ' ' || next == '#' || next == '\\' || next == '"') {
                        // Экранируем пробел, #, \, "
                        escape = true;
                        pos_++; // пропускаем обратный слеш
                        continue;
                    }
                    // Иначе просто добавляем обратный слеш как обычный символ
                }
            }
            if (c == '"') {
                // Начало кавычек
                inQuotes = true;
                pos_++;
                continue;
            }
        }

        // Обработка внутри кавычек или с экранированием
        if (inQuotes) {
            if (c == '"' && !escape) {
                inQuotes = false;
                pos_++;
                continue;
            }
            if (c == '\\' && !escape) {
                // Экранирование внутри кавычек
                if (pos_ + 1 < end_) {
                    char next = *(pos_ + 1);
                    if (next == 'n') { result.append('\n'); pos_ += 2; continue; }
                    if (next == 'r') { result.append('\r'); pos_ += 2; continue; }
                    if (next == 't') { result.append('\t'); pos_ += 2; continue; }
                    if (next == '\\') { result.append('\\'); pos_ += 2; continue; }
                    if (next == '"') { result.append('"'); pos_ += 2; continue; }
                    // Если неизвестное экранирование – просто добавляем символ
                }
            }
            result.append(c);
            pos_++;
            continue;
        }

        // Вне кавычек
        if (escape) {
            // Экранированный символ (пробел, #, \, ")
            result.append(c);
            escape = false;
            pos_++;
            continue;
        }

        // Обычный символ
        if (c == ' ' || c == '\t') {
            // Пробелы вне кавычек – если не экранированы, они разделители.
            // Но мы читаем одно значение, поэтому пробел добавляем (позже обрежем)
            result.append(c);
            pos_++;
            continue;
        }

        // Все остальное
        result.append(c);
        pos_++;
    }

    // Удаляем пробелы в конце (если есть)
    while (!result.isEmpty() && (result.back() == ' ' || result.back() == '\t')) {
        result.chop(1);
    }

    QString value = QString::fromUtf8(result);

    // Обработка продолжения строк (если разрешено)
    if (allowContinuation && !value.isEmpty() && value.endsWith('\\')) {
        value.chop(1);
        // Пропускаем перевод строки
        if (pos_ < end_ && (*pos_ == '\n' || *pos_ == '\r')) {
            if (*pos_ == '\r' && pos_ + 1 < end_ && *(pos_ + 1) == '\n')
                pos_ += 2;
            else
                pos_++;
            line_++;
            atLineStart_ = true;
        }
        // Рекурсивно читаем следующую строку
        QString next = readValue(allowContinuation);
        value += next;
    }

    return value;
}

void Lexer::error(const QString& msg) {
    qCritical() << "Lexer error in" << filename_ << "line" << line_ << ":" << msg;
}

} // namespace pmake::parser