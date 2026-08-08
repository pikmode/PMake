#include "util/string_utils.h"

namespace pmake::util {

QStringList split(const QString& s, QChar delimiter) {
    return s.split(delimiter, Qt::SkipEmptyParts);
}

QString trim(const QString& s) {
    return s.trimmed();
}

bool startsWith(const QString& s, const QString& prefix) {
    return s.startsWith(prefix);
}

bool endsWith(const QString& s, const QString& suffix) {
    return s.endsWith(suffix);
}

QString join(const QStringList& parts, const QString& glue) {
    return parts.join(glue);
}

QString replaceAll(const QString& s, const QString& from, const QString& to) {
    QString result = s;
    result.replace(from, to);
    return result;
}

QString toLower(const QString& s) {
    return s.toLower();
}

QString toUpper(const QString& s) {
    return s.toUpper();
}

QString unescapePath(const QString& path) {
    QString result = path;
    result.replace("\\ ", " ");
    result.replace("\\#", "#");
    result.replace("\\\\", "\\");
    return result;
}

QString quoteForShell(const QString& arg) {
    if (arg.isEmpty()) return QString("\"\"");
    // Если аргумент не содержит пробелов, кавычек и спецсимволов, возвращаем как есть
    bool needQuote = arg.contains(' ') || arg.contains('\t') || arg.contains('"') || arg.contains('\'') ||
                     arg.contains('$') || arg.contains('`') || arg.contains('\\') || arg.contains('|') ||
                     arg.contains('&') || arg.contains(';') || arg.contains('<') || arg.contains('>') ||
                     arg.contains('(') || arg.contains(')') || arg.contains('[') || arg.contains(']') ||
                     arg.contains('{') || arg.contains('}') || arg.contains('*') || arg.contains('?');
    if (!needQuote) return arg;

    // Экранируем обратные слеши и кавычки
    QString escaped = arg;
    escaped.replace('\\', "\\\\");
    escaped.replace('"', "\\\"");
    return "\"" + escaped + "\"";
}

} // namespace pmake::util