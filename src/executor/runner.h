// executor/runner.h
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QFuture>
#include <QHash> 

namespace pmake::executor {

struct Command {
    QString program;
    QStringList arguments;
    QString workingDirectory;
    bool captureOutput = true;
    QHash<QString, QString> env; // дополнительные переменные окружения
};

struct Result {
    int exitCode = -1;
    QString stdoutOutput;
    QString stderrOutput;
    double durationSeconds = 0.0;
};

/**
 * @brief Безопасный запуск внешних команд с таймаутом, событийным чтением вывода и защитой от deadlock.
 */
class Runner : public QObject {
    Q_OBJECT
public:
    // Синхронный запуск (блокирующий, но с защитой от зависания)
    static Result runSync(const Command& cmd);

    // Асинхронный запуск (возвращает QFuture)
    static QFuture<Result> runAsync(const Command& cmd);
};

} // namespace pmake::executor