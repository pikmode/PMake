#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace pmake::executor {

struct Command {
    std::string program;
    std::vector<std::string> arguments;
    std::string workingDirectory;
    bool captureOutput = true;
    std::unordered_map<std::string, std::string> env;
};

struct Result {
    int exitCode = -1;
    std::string stdoutOutput;
    std::string stderrOutput;
    double durationSeconds = 0.0;
};

class Runner {
public:
    static Result runSync(const Command& cmd);
};

} // namespace pmake::executor