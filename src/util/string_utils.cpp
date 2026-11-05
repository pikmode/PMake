#include "util/string_utils.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace pmake::util {

namespace {

void replace_inplace(std::string& s, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    std::size_t pos = 0;
    while ((pos = s.find(from.data(), pos, from.size())) != std::string::npos) {
        s.replace(pos, from.size(), to.data(), to.size());
        pos += to.size();
    }
}

} // namespace

std::vector<std::string> split(std::string_view s, char delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.find(delimiter, start);
        if (end == std::string_view::npos) end = s.size();
        if (end > start) result.emplace_back(s.substr(start, end - start));
        if (end == s.size()) break;
        start = end + 1;
    }
    return result;
}

std::string trim(std::string_view s) {
    std::size_t start = 0;
    while (start < s.size() &&
           (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) ++start;
    std::size_t end = s.size();
    while (end > start &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r')) --end;
    return std::string(s.substr(start, end - start));
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}
bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string join(const std::vector<std::string>& parts, std::string_view glue) {
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) result.append(glue);
        result += parts[i];
    }
    return result;
}

std::string replaceAll(std::string_view s, std::string_view from, std::string_view to) {
    std::string result(s);
    replace_inplace(result, from, to);
    return result;
}

std::string toLower(std::string_view s) {
    std::string result(s);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
std::string toUpper(std::string_view s) {
    std::string result(s);
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

std::string unescapePath(std::string_view path) {
    std::string result(path);
    replace_inplace(result, "\\ ", " ");
    replace_inplace(result, "\\#", "#");
    replace_inplace(result, "\\\\", "\\");
    return result;
}

std::string quoteForShell(std::string_view arg) {
    if (arg.empty()) return "\"\"";
    bool needQuote = false;
    for (char c : arg) {
        if (c == ' ' || c == '\t' || c == '"' || c == '\'' || c == '$' || c == '`' ||
            c == '\\' || c == '|' || c == '&' || c == ';' || c == '<' || c == '>' ||
            c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' ||
            c == '*' || c == '?') { needQuote = true; break; }
    }
    if (!needQuote) return std::string(arg);
    std::string escaped;
    escaped.reserve(arg.size() + 4);
    for (char c : arg) {
        if (c == '\\') escaped += "\\\\";
        else if (c == '"') escaped += "\\\"";
        else escaped += c;
    }
    return "\"" + escaped + "\"";
}

} // namespace pmake::util