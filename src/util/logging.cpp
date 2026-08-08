#include "util/logging.h"
#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QRegularExpression>
#include <QTextStream>
#include <QDateTime>

#ifdef Q_OS_WIN
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <unistd.h>
#endif

namespace pmake::logging {

static Config g_config;
static QFile g_logFile;
static QTextStream g_logStream;
static bool g_hasColor = false;
static bool g_verbose = false;
static QMutex g_mutex;

static void enableAnsiSupport() {
#ifdef Q_OS_WIN
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE || hOut == nullptr) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
    g_hasColor = true;
#else
    if (isatty(STDOUT_FILENO)) {
        g_hasColor = true;
    }
#endif
}

void init(const Config& cfg) {
    QMutexLocker locker(&g_mutex);
    g_config = cfg;
    if (cfg.log_to_file && !cfg.log_file.isEmpty()) {
        if (g_logFile.isOpen()) g_logFile.close();
        g_logFile.setFileName(cfg.log_file);
        if (g_logFile.open(QIODevice::Append | QIODevice::Text)) {
            g_logStream.setDevice(&g_logFile);
        } else {
            qWarning() << "Failed to open log file:" << cfg.log_file;
        }
    }
    if (cfg.color_output) {
        enableAnsiSupport();
    }
    // Принудительно отключаем цвет для INFO (зелёный) — оставляем только для WARN и ERR
}

void setLevel(Level level) {
    QMutexLocker locker(&g_mutex);
    g_config.min_level = level;
}

void setColorEnabled(bool enabled) {
    QMutexLocker locker(&g_mutex);
    g_config.color_output = enabled;
    if (enabled) {
        enableAnsiSupport();
    } else {
        g_hasColor = false;
    }
}

void setTimestampEnabled(bool enabled) {
    QMutexLocker locker(&g_mutex);
    g_config.show_timestamp = enabled;
}

void setVerbose(bool enabled) {
    QMutexLocker locker(&g_mutex);
    g_verbose = enabled;
    if (enabled) {
        g_config.min_level = Level::DEBUG;
    } else {
        if (g_config.min_level == Level::DEBUG) {
            g_config.min_level = Level::INFO;
        }
    }
}

bool isVerbose() {
    QMutexLocker locker(&g_mutex);
    return g_verbose;
}

static QString levelColor(Level level) {
    if (!g_hasColor || !g_config.color_output) return QString();
    switch (level) {
        case Level::WARN:  return "\033[33m";  // жёлтый
        case Level::ERR:   return "\033[31m";  // красный
        case Level::FATAL: return "\033[35m";  // пурпурный (оставим)
        default: return "";  // INFO, DEBUG без цвета
    }
}

static QString formatMessage(Level level, const QString& message) {
    if (level < g_config.min_level) return QString();
    QString result;
    if (g_config.show_timestamp) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        result += timestamp + " ";
    }
    QString color = levelColor(level);
    if (!color.isEmpty()) {
        result += color;
    }
    result += message;
    if (!color.isEmpty()) {
        result += "\033[0m";
    }
    return result;
}

void log(Level level, const QString& message) {
    if (level < g_config.min_level) return;
    QMutexLocker locker(&g_mutex);
    QString formatted = formatMessage(level, message);
    if (formatted.isEmpty()) return;

    if (g_config.log_to_console) {
        if (level != Level::DEBUG || g_verbose) {
            QTextStream(stderr) << formatted << Qt::endl;
        }
    }

    if (g_config.log_to_file && g_logFile.isOpen()) {
        QString plain = formatted;
        plain.remove(QRegularExpression("\\033\\[[0-9;]+m"));
        g_logStream << plain << Qt::endl;
        g_logStream.flush();
    }
}

} // namespace pmake::logging