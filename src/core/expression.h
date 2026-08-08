#pragma once

#include <QVariant>
#include <QString>
#include "core/state.h"

namespace pmake::core {

// Упрощённый интерпретатор выражений (будет расширяться)
class Expression {
public:
    static QVariant evaluate(const QString& expr, State* state, Scope* scope);
};

} // namespace pmake::core