// ui/console_ui.h
#pragma once

#include <QObject>

namespace pmake::ui {

class ConsoleUI : public QObject {
    Q_OBJECT
public:
    explicit ConsoleUI(QObject* parent = nullptr);

public slots:
    void onTaskStarted(const QString& displayName, int total);
    void onTaskFinished(const QString& name, bool success, double duration, const QString& errorMsg = QString());
    // Добавляем параметр duration
    void onBuildFinished(bool success, double durationSeconds);

private:
    int taskIndex_ = 0;
    int totalTasks_ = 0;
};

} // namespace pmake::ui