#include "core/state.h"

namespace pmake::core {

QString State::expandVars(const QString& input, const Scope* local_scope) const {
    QHash<QString, QString> empty;
    return expandVarsWithSpecial(input, local_scope, empty);
}

QString State::expandVarsWithSpecial(const QString& input,
                                     const Scope* local_scope,
                                     const QHash<QString, QString>& specialVars) const {
    if (input.indexOf('$') == -1 && input.indexOf('{') == -1) {
        return input;
    }

    QString result;
    result.reserve(input.size() + 64);

    int i = 0;
    const int len = input.length();
    while (i < len) {
        QChar c = input[i];
        if (c == '$') {
            if (i + 1 < len && input[i+1] == '{') {
                int end = input.indexOf('}', i + 2);
                if (end != -1) {
                    QString varName = input.mid(i + 2, end - i - 2);
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
                    if (found) {
                        result.append(value);
                    }
                    // Если не найдено, заменяем на пустую строку (как было в регулярке)
                    i = end + 1;
                    continue;
                }
            } else if (i + 1 < len && (input[i+1].isLetter() || input[i+1] == '_')) {
                int end = i + 2;
                while (end < len && (input[end].isLetterOrNumber() || input[end] == '_')) {
                    end++;
                }
                QString varName = input.mid(i + 1, end - i - 1);
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
                if (found) {
                    result.append(value);
                }
                i = end;
                continue;
            }
        } else if (c == '{') {
            int end = input.indexOf('}', i + 1);
            if (end != -1) {
                QString varName = input.mid(i + 1, end - i - 1);
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
                if (found) {
                    result.append(value);
                }
                i = end + 1;
                continue;
            }
        }
        result.append(c);
        i++;
    }
    return result;
}

} // namespace pmake::core