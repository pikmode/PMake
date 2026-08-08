// ui/console_ui.cpp
#include "ui/console_ui.h"
#include <QTextStream>
#include <iostream>
#include <QMutex>
#include <QDateTime>

namespace pmake::ui {

static QMutex outputMutex;

ConsoleUI::ConsoleUI(QObject* parent)
    : QObject(parent) {}

void ConsoleUI::onTaskStarted(const QString& displayName, int total) {
    ++taskIndex_;
    totalTasks_ = total;
    QMutexLocker locker(&outputMutex);
    QTextStream out(stdout);
    out << "[" << taskIndex_ << "/" << totalTasks_ << "] " << displayName << Qt::endl;
    out.flush();
}

void ConsoleUI::onTaskFinished(const QString& name, bool success, double duration, const QString& errorMsg) {
    if (!success) {
        QMutexLocker locker(&outputMutex);
        QTextStream out(stdout);
        out << "[ERROR] " << name << " failed";
        if (!errorMsg.isEmpty())
            out << ": " << errorMsg;
        out << Qt::endl;
        out.flush();
    }
}

void ConsoleUI::onBuildFinished(bool success, double durationSeconds) {
    QMutexLocker locker(&outputMutex);
    QTextStream out(stdout);
    if (success) {
        out << "Build succeeded";
    } else {
        out << "Build failed";
    }
    out << " in " << QString::number(durationSeconds, 'f', 1) << " s." << Qt::endl;
    out.flush();
}

} // namespace pmake::ui