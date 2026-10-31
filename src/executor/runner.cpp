// executor/runner.cpp
#include "executor/runner.h"
#include "util/logging.h"
#include "util/string_utils.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFileInfo>
#include <QDir>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace pmake::executor {

static QString findExecutable(const QString& program) {
    if (program.isEmpty()) return QString();

    auto checkFile = [](const QString& path) -> QString {
        QFileInfo fi(path);
        if (fi.exists() && fi.isExecutable()) {
            return fi.absoluteFilePath();
        }
        return QString();
    };

    // 1. Полный путь
    QString result = checkFile(program);
    if (!result.isEmpty()) return result;

#ifdef _WIN32
    // 2. На Windows пробуем добавить .exe
    if (!program.endsWith(".exe", Qt::CaseInsensitive)) {
        QString withExe = program + ".exe";
        result = checkFile(withExe);
        if (!result.isEmpty()) return result;
    }
#endif

    // 3. Поиск в PATH
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");
    QStringList pathDirs = path.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString& dir : pathDirs) {
        QString candidate = dir + "/" + program;
        result = checkFile(candidate);
        if (!result.isEmpty()) return result;

#ifdef _WIN32
        if (!program.endsWith(".exe", Qt::CaseInsensitive)) {
            candidate = dir + "/" + program + ".exe";
            result = checkFile(candidate);
            if (!result.isEmpty()) return result;
        }
#endif
    }

    return QString();
}

Result Runner::runSync(const Command& cmd) {
    QElapsedTimer timer;
    timer.start();

    QString programPath = findExecutable(cmd.program);
    if (programPath.isEmpty()) {
        Result res;
        res.exitCode = -1;
        res.stdoutOutput = "Program not found: " + cmd.program;
        res.stderrOutput = "Check PATH or provide absolute path.";
        LOG_ERROR(res.stdoutOutput);
        return res;
    }

    QString workDir = cmd.workingDirectory;
    if (workDir.isEmpty()) {
        workDir = QDir::currentPath();
    } else {
        workDir = QDir::toNativeSeparators(workDir);
        if (!QDir(workDir).exists()) {
            LOG_WARN(QString("Working directory does not exist: %1, using current").arg(workDir));
            workDir = QDir::currentPath();
        }
    }

    QProcess process;
    process.setWorkingDirectory(workDir);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = cmd.env.begin(); it != cmd.env.end(); ++it) {
        env.insert(it.key(), it.value());
    }
    process.setProcessEnvironment(env);

    process.setProgram(programPath);
    process.setArguments(cmd.arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);

    // FIX: команда и workdir уже напечатаны в Node::build (точная строка
    // с кавычками, ДО splitCommand). Раньше здесь печаталось "Starting: ..."
    // через cmd.arguments.join(' ') — splitCommand теряет кавычки, выглядело
    // как визуальная мешанина. Убрали дубль. Здесь — только результат в конце.

    process.start();
    if (!process.waitForStarted()) {
        Result res;
        res.exitCode = -1;
        res.stdoutOutput = "Failed to start process";
        res.stderrOutput = process.errorString();
        LOG_ERROR(QString("Process start failed: %1").arg(process.errorString()));
        return res;
    }

    QByteArray stdoutData, stderrData;
    QEventLoop loop;
    QTimer timeoutTimer;
    const int TIMEOUT_SECONDS = 3600;
    timeoutTimer.setSingleShot(true);

    bool verbose = logging::isVerbose();
    if (verbose) {
        QObject::connect(&process, &QProcess::readyReadStandardOutput, [&]() {
            QByteArray data = process.readAllStandardOutput();
            if (!data.isEmpty()) {
                QTextStream out(stdout);
                out << QString::fromUtf8(data);
                out.flush();
                stdoutData.append(data);
            }
        });
        QObject::connect(&process, &QProcess::readyReadStandardError, [&]() {
            QByteArray data = process.readAllStandardError();
            if (!data.isEmpty()) {
                QTextStream err(stderr);
                err << QString::fromUtf8(data);
                err.flush();
                stderrData.append(data);
            }
        });
    } else {
        QObject::connect(&process, &QProcess::readyReadStandardOutput,
                         [&]() { stdoutData.append(process.readAllStandardOutput()); });
        QObject::connect(&process, &QProcess::readyReadStandardError,
                         [&]() { stderrData.append(process.readAllStandardError()); });
    }

    QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     [&]() { loop.quit(); });

    QObject::connect(&timeoutTimer, &QTimer::timeout, [&]() {
        LOG_WARN("Command timed out, killing process");
        process.kill();
        process.waitForFinished(1000);
        loop.quit();
    });
    timeoutTimer.start(TIMEOUT_SECONDS * 1000);

    loop.exec();

    timeoutTimer.stop();
    stdoutData.append(process.readAllStandardOutput());
    stderrData.append(process.readAllStandardError());

    Result res;
    res.exitCode = process.exitCode();
    res.durationSeconds = timer.elapsed() / 1000.0;

    if (cmd.captureOutput) {
        res.stdoutOutput = QString::fromUtf8(stdoutData);
        res.stderrOutput = QString::fromUtf8(stderrData);
    }

    if (res.exitCode != 0) {
        LOG_ERROR(QString("    failed with exit code %1").arg(res.exitCode));
        if (!res.stderrOutput.isEmpty()) {
            LOG_ERROR("    STDERR:\n" + res.stderrOutput);
        }
        if (!res.stdoutOutput.isEmpty()) {
            LOG_ERROR("    STDOUT:\n" + res.stdoutOutput);
        }
        if (process.error() != QProcess::UnknownError) {
            LOG_ERROR(QString("    process error: %1").arg(process.errorString()));
        }
    } else {
        // FIX: с отступом 4 пробела — выровнено с "command:"/"workdir:" из
        // node_builder.cpp. Читается как продолжение блока [N/M].
        LOG_VERBOSE(QString("    succeeded in %1 s").arg(res.durationSeconds, 0, 'f', 2));
    }

    return res;
}

QFuture<Result> Runner::runAsync(const Command& cmd) {
    return QtConcurrent::run([cmd]() { return runSync(cmd); });
}

} // namespace pmake::executor