// ui/console_ui.h
#pragma once

#include <QObject>

namespace pmake::ui {

class ConsoleUI : public QObject {
    Q_OBJECT
public:
    explicit ConsoleUI(bool quiet = false, bool color = false,
                       QObject* parent = nullptr);

public slots:
    void onTaskStarted(const QString& displayName, int total);
    void onTaskFinished(const QString& name, bool success, double duration,
                        const QString& errorMsg = QString());
    void onBuildFinished(bool success, double durationSeconds);

private:
    int  taskIndex_  = 0;
    int  totalTasks_ = 0;
    bool quiet_      = false;
    bool color_      = false;
};

} // namespace pmake::ui