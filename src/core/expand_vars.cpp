#include "core/state.h"
#include <QRegularExpression>

namespace pmake::core {

QString State::expandVars(const QString& input, const Scope* local_scope) const {
    QHash<QString, QString> empty;
    return expandVarsWithSpecial(input, local_scope, empty);
}

QString State::expandVarsWithSpecial(const QString& input,
                                     const Scope* local_scope,
                                     const QHash<QString, QString>& specialVars) const {
    QString result = input;
    QRegularExpressionMatchIterator it = varRegex_.globalMatch(input);
    QList<QPair<int, int>> toReplace;
    QList<QString> replacements;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString varName;
        if (!match.captured(1).isEmpty()) {
            varName = match.captured(1);
        } else if (!match.captured(2).isEmpty()) {
            varName = match.captured(2);
        } else if (!match.captured(3).isEmpty()) {
            varName = match.captured(3);
        } else {
            continue;
        }
        QString value;
        bool found = false;
        auto specIt = specialVars.find(varName);
        if (specIt != specialVars.end()) { value = *specIt; found = true; }
        if (!found && local_scope) {
            auto opt = local_scope->get(varName);
            if (opt.has_value()) { value = *opt; found = true; }
        }
        if (!found) {
            auto opt = getGlobal(varName);
            if (opt.has_value()) { value = *opt; found = true; }
        }
        toReplace.append(qMakePair(match.capturedStart(), match.capturedLength()));
        replacements.append(found ? value : QString());
    }
    for (int i = toReplace.size() - 1; i >= 0; --i) {
        int start = toReplace[i].first;
        int len = toReplace[i].second;
        result.replace(start, len, replacements[i]);
    }
    return result;
}

} // namespace pmake::core