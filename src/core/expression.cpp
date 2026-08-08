#include "core/expression.h"
#include "core/builtins.h"
#include "util/logging.h"
#include <QRegularExpression>

namespace pmake::core {

QVariant Expression::evaluate(const QString& expr, State* state, Scope* scope) {
    QString trimmed = expr.trimmed();
    if (trimmed.isEmpty()) return QVariant();

    // Проверка на вызов функции
    QRegularExpression funcRegex(R"(^(\w+)\s*\(([^)]*)\)$)");
    QRegularExpressionMatch match = funcRegex.match(trimmed);
    if (match.hasMatch()) {
        QString funcName = match.captured(1);
        QString argsStr = match.captured(2).trimmed();
        QVector<QVariant> args;
        if (!argsStr.isEmpty()) {
            QStringList argList = argsStr.split(',', Qt::SkipEmptyParts);
            for (const QString& a : argList) {
                args.append(evaluate(a.trimmed(), state, scope));
            }
        }
        return Builtins::call(funcName, args, state, scope);
    }

    // Переменная {var}
    if (trimmed.startsWith('{') && trimmed.endsWith('}')) {
        QString varName = trimmed.mid(1, trimmed.length()-2);
        QVariant val;
        if (scope) {
            auto opt = scope->get(varName);
            if (opt.has_value()) val = QVariant(*opt);
        }
        if (!val.isValid()) {
            auto opt = state->getGlobal(varName);
            if (opt.has_value()) val = QVariant(*opt);
        }
        return val.isValid() ? val : QVariant(varName);
    }

    // Строка в кавычках
    if (trimmed.startsWith('"') && trimmed.endsWith('"')) {
        return QVariant(trimmed.mid(1, trimmed.length()-2));
    }

    // Число
    bool ok;
    int num = trimmed.toInt(&ok);
    if (ok) return QVariant(num);

    // bool
    if (trimmed == "true") return QVariant(true);
    if (trimmed == "false") return QVariant(false);

    // Список (не реализован)
    return QVariant(trimmed);
}

} // namespace pmake::core