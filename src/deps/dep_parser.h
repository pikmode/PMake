#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pmake::deps {

// Парсер .d-файлов (Makefile-формат):
//   target: in1 in2 in3 \
//     in4 in5
// Возвращает все inputs (без target). Поддерживает экранирование пробелов
// (`\ `) и переносы строк (`\<newline>`).
class DepParser {
public:
    static std::vector<std::string> parse(std::string_view depfile_path);
};

} // namespace pmake::deps