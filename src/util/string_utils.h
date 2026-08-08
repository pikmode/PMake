#pragma once

#include <QString>
#include <QStringList>

namespace pmake::util {

QStringList split(const QString& s, QChar delimiter);
QString trim(const QString& s);
bool startsWith(const QString& s, const QString& prefix);
bool endsWith(const QString& s, const QString& suffix);
QString join(const QStringList& parts, const QString& glue);
QString replaceAll(const QString& s, const QString& from, const QString& to);
QString toLower(const QString& s);
QString toUpper(const QString& s);

// Убирает экранирование в путях (для совместимости)
QString unescapePath(const QString& path);

// Экранирование для shell (кавычки, обратные слеши)
QString quoteForShell(const QString& arg);

} // namespace pmake::util