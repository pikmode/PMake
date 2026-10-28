// ui/console_ui.cpp
#include "ui/console_ui.h"
#include <QTextStream>
#include <QMutex>

namespace pmake::ui {

static QMutex outputMutex;

ConsoleUI::ConsoleUI(bool quiet, bool color, QObject* parent)
    : QObject(parent), quiet_(quiet), color_(color) {}

void ConsoleUI::onTaskStarted(const QString& displayName, int total) {
    ++taskIndex_;
    totalTasks_ = total;
    if (quiet_) return;   // -q: тихий режим, только финальный статус
    QMutexLocker locker(&outputMutex);
    QTextStream out(stdout);
    out << "[" << taskIndex_ << "/" << totalTasks_ << "] " << displayName << Qt::endl;
    out.flush();
}

void ConsoleUI::onTaskFinished(const QString& name, bool success,
                                double /*duration*/, const QString& errorMsg) {
    if (success) return;
    QMutexLocker locker(&outputMutex);
    QTextStream out(stdout);
    if (color_) out << "\033[31m";
    out << "[ERROR] " << name << " failed";
    if (color_) out << "\033[0m";
    if (!errorMsg.isEmpty()) out << ": " << errorMsg;
    out << Qt::endl;
    out.flush();
}

void ConsoleUI::onBuildFinished(bool success, double durationSeconds) {
    QMutexLocker locker(&outputMutex);
    QTextStream out(stdout);
    if (color_) out << (success ? "\033[32m" : "\033[31m");   // green / red
    out << (success ? "Build succeeded" : "Build failed");
    if (color_) out << "\033[0m";
    out << " in " << QString::number(durationSeconds, 'f', 1) << " s." << Qt::endl;
    out.flush();
}

} // namespace pmake::ui