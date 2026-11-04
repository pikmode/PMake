// parser/ninja-parser/parser.h
#pragma once

#include <memory>
#include "core/state.h"
#include "util/string_pool.h"

namespace pmake::ninja {   // ← было pmake::parser

class Parser {
public:
    // Парсит .ninja файл, возвращает State.
    // Единственное отличие от pmake DSL — нет config/platform.
    std::unique_ptr<core::State> parse(const QString& filename);
};

} // namespace pmake::ninja