#include "ui/console_ui.h"

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
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

// CHANGED: без description — он уже в displayName.
// Печатаем только command и workdir.
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
    line += "[ERROR] ";
    line += name;
    if (!errorMsg.empty()) {
        line += " (";
        line += errorMsg;
        line += ")";
    } else {
        line += " failed";
    }
    if (color_) line += "\033[0m";
    line += '\n';

    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

void ConsoleUI::onBuildFinished(bool success, double durationSeconds) {
    if (quiet_) return;

    std::lock_guard<std::mutex> lk(outputMutex_);

    // Noop: ни одной задачи не запускалось → "All targets up to date".
    // Pre-build failure: taskIndex_ == 0, но сборка провалилась до старта —
    // детали уже напечатаны LOG_ERROR/graph.cpp, не дублируем.
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
    } else {
        if (taskIndex_ == 0) return;
        if (color_) line += "\033[31m";
        line += "Build failed";
        if (color_) line += "\033[0m";
        line += std::format(" in {:.1f} s.\n", durationSeconds);
    }

    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

} // namespace pmake::ui