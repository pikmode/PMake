#include "util/logging.h"
#include "internal/config.h" // NEW: для PMAKE_MSG_PREFIX

#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
#else
    #include <unistd.h>
#endif

namespace pmake::logging {

namespace {

Config g_config;
std::ofstream g_logFile;
bool g_hasColor = false;
bool g_verbose = false;
std::mutex g_mutex;

void enableAnsiSupport() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE || hOut == nullptr) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
    g_hasColor = true;
#else
    if (isatty(STDOUT_FILENO)) g_hasColor = true;
#endif
}

std::string_view levelColor(Level level) {
    if (!g_hasColor || !g_config.color_output) return {};
    switch (level) {
        case Level::WARN:  return "\033[33m";
        case Level::ERR:   return "\033[31m";
        case Level::FATAL: return "\033[35m";
        default: return {};
    }
}

std::string formatMessage(Level level, std::string_view message) {
    std::string result;
    if (g_config.show_timestamp) {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        result += buf;
        result += ' ';
    }
    
    auto color = levelColor(level);
    if (!color.empty()) result += color;
    
    // NEW: Единый префикс для ошибок и предупреждений
    if (level == Level::ERR || level == Level::FATAL) {
        result += PMAKE_MSG_PREFIX "error: ";
    } else if (level == Level::WARN) {
        result += PMAKE_MSG_PREFIX "warning: ";
    }

    result += message;
    if (!color.empty()) result += "\033[0m";
    return result;
}

} // namespace

void init(const Config& cfg) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_config = cfg;
    if (cfg.log_to_file && !cfg.log_file.empty()) {
        if (g_logFile.is_open()) g_logFile.close();
        g_logFile.open(cfg.log_file, std::ios::app);
        if (!g_logFile) {
            std::fprintf(stderr,
                "Warning: failed to open log file: %s\n", cfg.log_file.c_str());
        }
    }
    if (cfg.color_output) enableAnsiSupport();
}

void setLevel(Level level) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_config.min_level = level;
}

void setColorEnabled(bool enabled) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_config.color_output = enabled;
    if (enabled) enableAnsiSupport();
    else g_hasColor = false;
}

void setTimestampEnabled(bool enabled) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_config.show_timestamp = enabled;
}

void setVerbose(bool enabled) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_verbose = enabled;
    if (enabled) {
        g_config.min_level = Level::DEBUG;
    } else if (g_config.min_level == Level::DEBUG) {
        g_config.min_level = Level::INFO;
    }
}

bool isVerbose() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_verbose;
}

void log(Level level, std::string_view message) {
    if (level < g_config.min_level) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_config.log_to_console && !g_config.log_to_file) return;
    if (level == Level::DEBUG && !g_verbose) return;

    std::string formatted = formatMessage(level, message);

    if (g_config.log_to_console) {
        std::fwrite(formatted.data(), 1, formatted.size(), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }

    if (g_config.log_to_file && g_logFile.is_open()) {
        g_logFile << formatted << '\n';
        g_logFile.flush();
    }
}

} // namespace pmake::logging