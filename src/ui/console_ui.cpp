#include "ui/console_ui.h"

#include <cstdio>
#include <format>
#include <string>

namespace pmake::ui {

ConsoleUI::ConsoleUI(bool quiet, bool color)
    : quiet_(quiet), color_(color) {}

void ConsoleUI::onTaskStarted(std::string_view displayName, int total) {
    ++taskIndex_;
    totalTasks_ = total;
    if (quiet_) return;
    std::lock_guard<std::mutex> lk(outputMutex_);
    auto line = std::format("[{}/{}] {}\n", taskIndex_, totalTasks_, displayName);
    std::fwrite(line.data(), 1, line.size(), stdout);
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
    line += " failed";
    if (color_) line += "\033[0m";
    if (!errorMsg.empty()) {
        line += ": ";
        line += errorMsg;
    }
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

void ConsoleUI::onBuildFinished(bool success, double durationSeconds) {
    std::lock_guard<std::mutex> lk(outputMutex_);
    std::string line;
    if (color_) line += success ? "\033[32m" : "\033[31m";
    line += success ? "Build succeeded" : "Build failed";
    if (color_) line += "\033[0m";
    line += std::format(" in {:.1f} s.\n", durationSeconds);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

} // namespace pmake::ui