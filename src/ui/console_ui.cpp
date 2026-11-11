#include "ui/console_ui.h"
#include "internal/config.h"

#include <cstdio>
#include <format>
#include <string>

namespace pmake::ui {

ConsoleUI::ConsoleUI(bool quiet, bool color, bool verbose)
    : quiet_(quiet), color_(color), verbose_(verbose) {}

void ConsoleUI::onTaskStarted(std::string_view displayName, int total) {
    ++taskIndex_;
    totalTasks_ = total;
    if (quiet_) return;
    std::lock_guard<std::mutex> lk(outputMutex_);
    auto line = std::format("[{}/{}] {}\n", taskIndex_, totalTasks_, displayName);
    std::fwrite(line.data(), 1, line.size(), stdout); // Это оставляем в stdout
    std::fflush(stdout);
}

void ConsoleUI::onTaskVerbose(std::string_view command,
                               std::string_view workdir) {
    if (quiet_ || !verbose_) return;
    std::lock_guard<std::mutex> lk(outputMutex_);

    std::string out;
    out.reserve(command.size() + workdir.size() + 32);
    out += "  command:     ";
    out += command;
    out += '\n';
    out += "  workdir:     ";
    out += workdir;
    out += '\n';

    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
}

void ConsoleUI::onTaskFinished(std::string_view name, bool success,
                                double /*duration*/, std::string_view errorMsg) {
    if (success) return;
    std::lock_guard<std::mutex> lk(outputMutex_);

    std::string line;
    if (color_) line += "\033[31m";

    line += PMAKE_MSG_PREFIX "error: task '";
    line += name;
    line += "' failed";
    if (!errorMsg.empty()) {
        line += " (";
        line += errorMsg;
        line += ")";
    }
    if (color_) line += "\033[0m";
    line += '\n';

    // CHANGED: Ошибки выводим в stderr!
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
}

void ConsoleUI::onBuildFinished(bool success, double durationSeconds, bool interrupted) {
    if (quiet_) return;

    std::lock_guard<std::mutex> lk(outputMutex_);

    std::string line;
    if (success) {
        if (taskIndex_ == 0) {
            line = "All targets up to date\n";
        } else {
            if (color_) line += "\033[32m";
            line += "Build succeeded";
            if (color_) line += "\033[0m";
            line += std::format(" in {:.1f} s.\n", durationSeconds);
        }
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fflush(stdout);
    } else {
        if (taskIndex_ == 0 && !interrupted) return;
        if (color_) line += "\033[31m";
        line += PMAKE_MSG_PREFIX "error: ";
        line += interrupted ? "Build interrupted" : "Build failed";
        if (color_) line += "\033[0m";
        line += std::format(" in {:.1f} s.\n", durationSeconds);

        // CHANGED: Ошибки выводим в stderr!
        std::fwrite(line.data(), 1, line.size(), stderr);
        std::fflush(stderr);
    }
}

} // namespace pmake::ui