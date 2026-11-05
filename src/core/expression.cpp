#include "core/expression.h"
#include "core/builtins.h"
#include "core/state.h"
#include "util/string_utils.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace pmake::core {

Value Expression::evaluate(std::string_view expr, State* state, Scope* scope) {
    std::string trimmed = util::trim(expr);
    if (trimmed.empty()) return Value();

    // function call: name(args)
    auto paren = trimmed.find('(');
    if (paren != std::string::npos && trimmed.back() == ')') {
        std::string funcName = util::trim(
            std::string_view(trimmed).substr(0, paren));
        std::string argsStr = util::trim(
            std::string_view(trimmed).substr(paren + 1, trimmed.size() - paren - 2));

        std::vector<Value> args;
        if (!argsStr.empty()) {
            // split by comma (без вложенности)
            std::string cur;
            for (char c : argsStr) {
                if (c == ',') {
                    args.push_back(evaluate(cur, state, scope));
                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }
            args.push_back(evaluate(cur, state, scope));
        }
        return Builtins::call(funcName, args, state, scope);
    }

    // {var}
    if (trimmed.front() == '{' && trimmed.back() == '}' && trimmed.size() >= 2) {
        std::string varName = trimmed.substr(1, trimmed.size() - 2);
        if (scope) {
            auto opt = scope->get(varName);
            if (opt.has_value()) return Value(*opt);
        }
        auto opt = state->getGlobal(varName);
        if (opt.has_value()) return Value(*opt);
        return Value(varName);
    }

    // "string"
    if (trimmed.front() == '"' && trimmed.back() == '"' && trimmed.size() >= 2) {
        return Value(trimmed.substr(1, trimmed.size() - 2));
    }

    // number
    bool isNum = !trimmed.empty();
    for (char c : trimmed) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-' && c != '.') {
            isNum = false;
            break;
        }
    }
    if (isNum) {
        try {
            return Value(std::stod(trimmed));
        } catch (...) {
            return Value(trimmed);
        }
    }

    // bool
    if (trimmed == "true")  return Value(true);
    if (trimmed == "false") return Value(false);

    // fallback: as string
    return Value(trimmed);
}

} // namespace pmake::core