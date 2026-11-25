#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pmake::executor {

enum class OutputStream {
    Stdout,
    Stderr,
};

struct Command {
    std::string program;
    std::vector<std::string> arguments;
    std::string shellCommand;
    std::string workingDirectory;
    bool captureOutput = true;
    bool runInShell = false;
    std::unordered_map<std::string, std::string> env;
    std::function<void(OutputStream, std::string_view)> onOutput;
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