#pragma once

#include <string>
#include <string_view>

#include "core/ast.h"

namespace pmake::parser {

// Чтение версии из internal/config.h
double getPmakeVersion();

// Форматирование сообщения об ошибке
std::string formatError(std::string_view filename, int line, int column,
                        std::string_view msg, std::string_view token = {});

// NEW (#4): глубокая копия AST. Нужна в двух местах:
//   1. parseFunction — клонирует body при регистрации FunctionDef
//   2. evaluateIncludeImpl — клонирует functions из sub-парсера в parent
//      (иначе double-free: sub и parent оба удалят func.body при деструкторе)
core::AstNode* cloneAst(core::AstNode* n);

} // namespace pmake::parser