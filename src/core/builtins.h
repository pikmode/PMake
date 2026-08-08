#pragma once

#include <QVariant>
#include <QVector>
#include <QString>
#include "core/state.h"

namespace pmake::core {

class Builtins {
public:
    // Вызов встроенной функции по имени
    static QVariant call(const QString& name,
                         const QVector<QVariant>& args,
                         State* state,
                         Scope* scope);
};

} // namespace pmake::core