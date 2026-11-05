#pragma once

#include <string>
#include <string_view>

namespace pmake::parser {

// Чтение версии из version.txt
double getPmakeVersion();

// Форматирование сообщения об ошибке
std::string formatError(std::string_view filename, int line, int column,
                        std::string_view msg, std::string_view token = {});

} // namespace pmake::parser