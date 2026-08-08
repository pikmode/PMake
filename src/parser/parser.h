// parser/parser.h
// Парсер PMake 1.0 — поддерживает все конструкции языка.
// Требует наличия обновлённого ast.h с новыми типами узлов.
#pragma once

#include <memory>
#include "core/state.h"

namespace pmake::parser {

/**
 * @brief Основной класс парсера PMake.
 *
 * Поддерживает синтаксис PMake 1.0:
 * - pmake_required_version = 1.0
 * - rule, build, config, platform, if, foreach, function, alias, include
 * - switch, while, break, continue, import, namespace, const, enum
 * - полноценные выражения с приоритетами операторов
 * - интерполяция строк ${var}
 * - списки и базовые типы (числа, строки, булевы, null)
 */
class Parser {
public:
    /**
     * @brief Разбор файла PMake.
     * @param filename Путь к файлу.
     * @param config Имя конфигурации (опционально).
     * @param platform Имя платформы (опционально).
     * @return Состояние сборки (State) или nullptr при ошибке.
     */
    std::unique_ptr<core::State> parse(const QString& filename,
                                       const QString& config = QString(),
                                       const QString& platform = QString());

    /**
     * @brief Разбор строки с кодом PMake (для тестов).
     * @param data Строка с кодом.
     * @param filename Имя файла для отображения в сообщениях об ошибках.
     * @param config Имя конфигурации (опционально).
     * @param platform Имя платформы (опционально).
     * @return Состояние сборки (State) или nullptr при ошибке.
     */
    std::unique_ptr<core::State> parseFromString(const QByteArray& data,
                                                 const QString& filename = "test.pmake",
                                                 const QString& config = QString(),
                                                 const QString& platform = QString());
};

} // namespace pmake::parser