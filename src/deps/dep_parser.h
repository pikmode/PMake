// deps/dep_parser.h
#pragma once

#include <QString>
#include <QVector>

namespace pmake::deps {

class DepParser {
public:
    // Парсит файл .d и возвращает список входных файлов для данного выходного
    // Поддерживает многострочные зависимости, пути с пробелами (в кавычках), экранирование
    static QVector<QString> parse(const QString& depfile_path);
};

} // namespace pmake::deps