#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pmake::util {

std::vector<std::string> split(std::string_view s, char delimiter);
std::string trim(std::string_view s);
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);
std::string join(const std::vector<std::string>& parts, std::string_view glue);
std::string replaceAll(std::string_view s, std::string_view from, std::string_view to);
std::string toLower(std::string_view s);
std::string toUpper(std::string_view s);
std::string unescapePath(std::string_view path);
std::string quoteForShell(std::string_view arg);

} // namespace pmake::util