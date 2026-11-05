#include "parser/parser_utils.h"

#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>

namespace pmake::parser {

namespace {

std::optional<std::string> read_version_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    std::string data;
    data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return data;
}

std::string trim(std::string_view s) {
    std::size_t start = 0;
    while (start < s.size() &&
           (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r')) {
        --end;
    }
    return std::string(s.substr(start, end - start));
}

} // namespace

double getPmakeVersion() {
    // Qt resource fallback (:/version.txt) убран — без Qt его нет.
    // Если нужен embed версии — используй CMake configure_file → #include "version.h".
    auto data = read_version_file("version.txt");
    if (!data) {
        std::println(std::cerr, "version.txt not found, using default 1.0");
        return 1.0;
    }

    std::string content = trim(*data);
    if (content.empty()) {
        std::println(std::cerr, "Invalid version in version.txt, using default 1.0");
        return 1.0;
    }

    double version = 0.0;
    auto [ptr, ec] = std::from_chars(content.data(),
                                     content.data() + content.size(),
                                     version);
    if (ec != std::errc{} || ptr != content.data() + content.size()) {
        std::println(std::cerr, "Invalid version in version.txt, using default 1.0");
        return 1.0;
    }
    return version;
}

std::string formatError(std::string_view filename, int line, int column,
                        std::string_view msg, std::string_view token) {
    if (token.empty())
        return std::format("{}:{}:{}: error: {}", filename, line, column, msg);
    else
        return std::format("{}:{}:{}: error: {}\n    {}", filename, line, column, msg, token);
}

} // namespace pmake::parser