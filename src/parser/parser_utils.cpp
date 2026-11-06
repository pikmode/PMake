#include "parser/parser_utils.h"
#include "internal/config.h"

#include <format>
#include <string>
#include <string_view>

namespace pmake::parser {

double getPmakeVersion() {
    // Единый источник правды — internal/config.h.
    // Никаких version.txt, никаких warning'ов в stderr.
    return PMAKE_VERSION_NUM;
}

std::string formatError(std::string_view filename, int line, int column,
                        std::string_view msg, std::string_view token) {
    if (token.empty())
        return std::format("{}:{}:{}: error: {}", filename, line, column, msg);
    else
        return std::format("{}:{}:{}: error: {}\n    {}", filename, line, column, msg, token);
}

} // namespace pmake::parser