// parser/parser.h
#pragma once

#include <memory>
#include "core/state.h"
#include "util/string_pool.h"

namespace pmake::parser {

class Parser {
public:
    // Парсит файл .ninja или .pmake, возвращает готовое состояние
    std::unique_ptr<core::State> parse(const QString& filename);

private:
    // внутренняя реализация скрыта
};

} // namespace pmake::parser