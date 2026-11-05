#include "core/state.h"

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>

namespace pmake::core {

std::string State::expandVars(std::string_view input, const Scope* local_scope) const {
    static const std::unordered_map<std::string, std::string> empty;
    return expandVarsWithSpecial(input, local_scope, empty);
}

std::string State::expandVarsWithSpecial(
    std::string_view input,
    const Scope* local_scope,
    const std::unordered_map<std::string, std::string>& specialVars) const {

    if (input.find('$') == std::string_view::npos &&
        input.find('{') == std::string_view::npos) {
        return std::string(input);
    }

    std::string result;
    result.reserve(input.size() + 64);

    std::size_t i = 0;
    const std::size_t len = input.size();

    auto resolve = [&](std::string_view varName) -> std::pair<std::string, bool> {
        auto specIt = specialVars.find(std::string(varName));
        if (specIt != specialVars.end()) return {specIt->second, true};
        if (local_scope) {
            auto opt = local_scope->get(varName);
            if (opt.has_value()) return {*opt, true};
        }
        auto opt = getGlobal(varName);
        if (opt.has_value()) return {*opt, true};
        return {{}, false};
    };

    while (i < len) {
        char c = input[i];
        if (c == '$') {
            if (i + 1 < len && input[i + 1] == '{') {
                auto end = input.find('}', i + 2);
                if (end != std::string_view::npos) {
                    auto [value, found] = resolve(input.substr(i + 2, end - i - 2));
                    if (found) result += value;
                    i = end + 1;
                    continue;
                }
            } else if (i + 1 < len &&
                       (std::isalpha(static_cast<unsigned char>(input[i + 1])) || input[i + 1] == '_')) {
                std::size_t end = i + 2;
                while (end < len &&
                       (std::isalnum(static_cast<unsigned char>(input[end])) || input[end] == '_')) {
                    ++end;
                }
                auto [value, found] = resolve(input.substr(i + 1, end - i - 1));
                if (found) result += value;
                i = end;
                continue;
            }
        } else if (c == '{') {
            auto end = input.find('}', i + 1);
            if (end != std::string_view::npos) {
                auto [value, found] = resolve(input.substr(i + 1, end - i - 1));
                if (found) result += value;
                i = end + 1;
                continue;
            }
        }
        result += c;
        ++i;
    }
    return result;
}

} // namespace pmake::core