#pragma once

#include <string>
#include <string_view>

namespace pmake::logging {

enum class Level {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERR   = 3,
    FATAL = 4
};

struct Config {
    Level min_level = Level::INFO;
    bool log_to_console = true;
    bool log_to_file = false;
    std::string log_file = "pmake.log";
    bool color_output = true;
    bool show_timestamp = false;
    bool show_level = false;
};

void init(const Config& cfg = Config());
void setLevel(Level level);
void setColorEnabled(bool enabled);
void setTimestampEnabled(bool enabled);
void setVerbose(bool enabled);
bool isVerbose();

void log(Level level, std::string_view message);

#define LOG_DEBUG(msg)  pmake::logging::log(pmake::logging::Level::DEBUG, msg)
#define LOG_INFO(msg)   pmake::logging::log(pmake::logging::Level::INFO,  msg)
#define LOG_WARN(msg)   pmake::logging::log(pmake::logging::Level::WARN,  msg)
#define LOG_ERROR(msg)  pmake::logging::log(pmake::logging::Level::ERR,   msg)
#define LOG_FATAL(msg)  pmake::logging::log(pmake::logging::Level::FATAL, msg)

#define LOG_VERBOSE(msg) do { if (pmake::logging::isVerbose()) \
    pmake::logging::log(pmake::logging::Level::DEBUG, msg); } while(0)

} // namespace pmake::logging