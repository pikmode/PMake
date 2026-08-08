#pragma once

#include <QString>

namespace pmake::parser {

// Чтение версии из version.txt
double getPmakeVersion();

// Форматирование сообщения об ошибке
QString formatError(const QString& filename, int line, int column,
                    const QString& msg, const QString& token = QString());

} // namespace pmake::parser