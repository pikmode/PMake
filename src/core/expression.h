#pragma once

#include <string_view>

#include "core/value.h"

namespace pmake::core {

class State;
class Scope;

// Упрощённый интерпретатор выражений (для legacy-кода; новый парсер
// использует ParserImpl::evaluateExpression напрямую).
class Expression {
public:
    static Value evaluate(std::string_view expr, State* state, Scope* scope);
};

} // namespace pmake::core