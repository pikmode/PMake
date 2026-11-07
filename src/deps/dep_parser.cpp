#include "deps/dep_parser.h"
#include "util/filesystem.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace pmake::deps {

namespace {

// Заменяет `\ ` → пробел, `\<newline>` → продолжение строки.
// Возвращает «логические» токены.
//
// NEW: не режет `:` внутри Windows-путей вида "C:\..." или "C:/...".
//      Drive letter детектируется как "одна alpha-буква перед `:` + `\`/`/` после".
std::vector<std::string> split_tokens(std::string_view content) {
    std::vector<std::string> tokens;
    std::string cur;
    bool have_target = false;

    std::size_t i = 0;
    while (i < content.size()) {
        char c = content[i];

        if (c == '\\' && i + 1 < content.size()) {
            char n = content[i + 1];
            if (n == '\n' || n == '\r') {
                i += 2;
                if (n == '\r' && i < content.size() && content[i] == '\n') ++i;
                continue;
            }
            if (n == ' ' || n == '#' || n == ':') {
                cur.push_back(n);
                i += 2;
                continue;
            }
            cur.push_back(c);
            ++i;
            continue;
        }

        if (c == ':') {
            // NEW: детект Windows drive letter.
            //   cur = "C"  &&  next = '\\' или '/'  → это "C:\" или "C:/"
            if (i + 1 < content.size() &&
                (content[i + 1] == '\\' || content[i + 1] == '/') &&
                cur.size() == 1 &&
                std::isalpha(static_cast<unsigned char>(cur[0]))) {
                cur.push_back(':');
                ++i;
                continue;
            }
            // Разделитель target:inputs
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            have_target = true;
            ++i;
            while (i < content.size() && (content[i] == ' ' || content[i] == '\t')) ++i;
            continue;
        }

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            ++i;
            continue;
        }

        cur.push_back(c);
        ++i;
    }
    if (!cur.empty()) tokens.push_back(cur);

    // Первый токен до ':' — target. Убираем его.
    if (have_target && !tokens.empty()) tokens.erase(tokens.begin());
    return tokens;
}

} // namespace

std::vector<std::string> DepParser::parse(std::string_view depfile_path) {
    std::string data;
    if (!util::readFile(depfile_path, data)) return {};
    return split_tokens(data);
}

} // namespace pmake::deps