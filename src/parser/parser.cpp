#include "parser/parser.h"
#include "parser/parser_impl.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>

namespace pmake::parser {

namespace {

// Читает весь файл в std::string. Возвращает std::nullopt при ошибке.
// TODO: заменить на pikrt::fs::read_all, когда библиотека будет готова.
std::optional<std::string> read_file(std::string_view path) {
    std::ifstream f(std::filesystem::path(std::string(path)), std::ios::binary);
    if (!f) return std::nullopt;

    f.seekg(0, std::ios::end);
    std::streampos size = f.tellg();
    if (size < 0) return std::nullopt;
    f.seekg(0, std::ios::beg);

    std::string data(static_cast<std::size_t>(size), '\0');
    if (!f.read(data.data(), size)) return std::nullopt;
    return data;
}

} // namespace

std::unique_ptr<core::State> Parser::parse(std::string_view filename,
                                          std::string_view config,
                                          std::string_view platform) {
    auto data = read_file(filename);
    if (!data) {
        std::println(std::cerr, "Cannot read file: {}", filename);
        return nullptr;
    }
    ParserImpl impl(*data, filename, config, platform);
    return impl.parse();
}

std::unique_ptr<core::State> Parser::parseFromString(std::string_view data,
                                                     std::string_view filename,
                                                     std::string_view config,
                                                     std::string_view platform) {
    ParserImpl impl(data, filename, config, platform);
    return impl.parse();
}

} // namespace pmake::parser