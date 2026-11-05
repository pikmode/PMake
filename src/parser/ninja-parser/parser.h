// parser/ninja-parser/parser.h
#pragma once

#include <memory>
#include <string_view>
#include "core/state.h"

namespace pmake::ninja {

class Parser {
public:
    // Парсит .ninja файл, возвращает State.
    // Единственное отличие от pmake DSL — нет config/platform.
    std::unique_ptr<core::State> parse(std::string_view filename);
};

} // namespace pmake::ninja