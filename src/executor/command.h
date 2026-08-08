// executor/command.h
#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace pmake::executor {

struct Command {
    std::string executable;                     // программа
    std::vector<std::string> args;              // аргументы
    std::unordered_map<std::string, std::string> env; // переменные окружения
    std::string working_dir;                    // рабочий каталог (пусто = текущий)
    std::string stdin_input;                    // если не пусто, передать на stdin
    bool capture_stdout = true;                 // перехватывать stdout?
    bool capture_stderr = true;                 // перехватывать stderr?
};

struct Result {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
    double duration_seconds = 0.0;
    bool timed_out = false;
};

} // namespace pmake::executor