// parser/parser.h
// Парсер PMake 1.0 — поддерживает все конструкции языка.
#pragma once

#include <memory>
#include <string_view>
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
    std::unique_ptr<core::State> parse(std::string_view filename,
                                       std::string_view config = {},
                                       std::string_view platform = {});

    /**
     * @brief Разбор строки с кодом PMake (для тестов).
     * @param data Строка с кодом.
     * @param filename Имя файла для отображения в сообщениях об ошибках.
     * @param config Имя конфигурации (опционально).
     * @param platform Имя платформы (опционально).
     * @return Состояние сборки (State) или nullptr при ошибке.
     */
    std::unique_ptr<core::State> parseFromString(std::string_view data,
                                                 std::string_view filename = "test.pmake",
                                                 std::string_view config = {},
                                                 std::string_view platform = {});
};

} // namespace pmake::parser