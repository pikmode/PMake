#include <QCoreApplication>
#include <QCommandLineParser>
#include <QEventLoop>
#include <QDebug>
#include <QThread>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <iostream>
#include <memory>
#include <csignal>
#include <QTimer>
#include <QDir>
#include <cstdlib>
#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>
#include <QFutureWatcher>

#include "core/engine.h"
#include "core/state.h"
#include "parser/parser.h"
#include "util/logging.h"
#include "util/filesystem.h" // NEW
#include "ui/console_ui.h"
#include "core/cache.h"
#include "deps/deps_log.h"
#include "deps/dep_parser.h"

using namespace pmake::core;

static QCoreApplication* g_app = nullptr;

static void signalHandler(int sig) {
    (void)sig;
    if (g_app) g_app->quit();
    else std::exit(1);
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    g_app = &app;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#ifdef SIGBREAK
    signal(SIGBREAK, signalHandler);
#endif

    QCoreApplication::setApplicationName("PMake");
    QCoreApplication::setApplicationVersion("1.0.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("PMake modern build system");
    parser.addHelpOption();

    QCommandLineOption versionOption({"V", "version"},
                                     "Display version information");
    parser.addOption(versionOption);

    QCommandLineOption fileOption({"f", "file"}, "Use <file> as build file", "file");
    parser.addOption(fileOption);

    QCommandLineOption jobsOption({"j", "jobs"}, "Run <N> jobs in parallel", "N");
    parser.addOption(jobsOption);

    QCommandLineOption cleanOption("clean", "Clean all outputs");
    parser.addOption(cleanOption);

    QCommandLineOption configOption("config", "Build configuration (debug/release)", "config", "release");
    parser.addOption(configOption);

    QCommandLineOption platformOption("platform", "Target platform (windows/linux/darwin)", "platform");
    parser.addOption(platformOption);

    QCommandLineOption verboseOption({"v", "verbose"}, "Show detailed command output");
    parser.addOption(verboseOption);

    QCommandLineOption quietOption({"q", "quiet"}, "Minimal output (errors + final status only)");
    parser.addOption(quietOption);

    QCommandLineOption dryRunOption({"n", "dry-run"}, "Show what would be built without executing commands");
    parser.addOption(dryRunOption);

    QCommandLineOption noColorOption("no-color", "Disable colored output");
    parser.addOption(noColorOption);

    QCommandLineOption licenseOption("license", "Show license and third-party notices");
    parser.addOption(licenseOption);

    parser.addPositionalArgument("command", "Command: build, clean, help", "build");
    parser.addPositionalArgument("target", "Target to build", "target");

    parser.process(app);

    // --version / -V
    if (parser.isSet(versionOption)) {
        QTextStream out(stdout);
        out << QCoreApplication::applicationName()
            << " " << QCoreApplication::applicationVersion() << "\n";
        out.flush();
        return 0;
    }

    // --license
    if (parser.isSet(licenseOption)) {
        QTextStream out(stdout);
        out << QCoreApplication::applicationName()
            << " " << QCoreApplication::applicationVersion() << "\n";
        out << "Copyright (C) 2026 PikMode\n\n";
        out << "This software is licensed under the PPL v1 License.\n";
        out << "See LICENSES/PPL_V1.txt for the full text.\n\n";
        out << "Third-party libraries:\n\n";
        out << "  Qt 6.8.3 - GNU Lesser General Public License v3 (LGPLv3)\n";
        out << "    https://www.qt.io/\n";
        out << "    Dynamically linked: Qt6Core, Qt6Concurrent.\n";
        out << "    You may relink PMake against a modified version of Qt.\n\n";
        out << "Build tools used to produce this binary:\n";
        out << "  CMake 3.30             BSD 3-Clause\n";
        out << "  Ninja                  Apache 2.0\n";
        out << "  MinGW-w64 (g++ 16.1)   GPL v3 + runtime exception\n";
        out << "  Python 3.12            PSF License\n";
        out.flush();
        return 0;
    }

    QStringList args = parser.positionalArguments();
    QString command = args.isEmpty() ? "build" : args.at(0);
    QString target  = args.size() > 1 ? args.at(1) : QString();

    if (command == "help") parser.showHelp(0);

    QString buildFile = parser.value(fileOption);
    if (buildFile.isEmpty()) {
        if (QFile::exists("build.pmake")) buildFile = "build.pmake";
        else {
            LOG_ERROR("No build file found (build.pmake)");
            return 1;
        }
    }

    int jobs = QThread::idealThreadCount();
    if (parser.isSet(jobsOption)) {
        bool ok;
        int val = parser.value(jobsOption).toInt(&ok);
        if (ok && val > 0) jobs = val;
    }

    bool isVerbose = parser.isSet(verboseOption);
    bool isQuiet   = parser.isSet(quietOption);
    bool dryRun    = parser.isSet(dryRunOption);

    bool noColorEnv = qEnvironmentVariableIsSet("NO_COLOR");
    bool useColor = !parser.isSet(noColorOption) && !noColorEnv;

    pmake::logging::Config logCfg;
    logCfg.log_to_console  = true;
    logCfg.color_output    = useColor;
    logCfg.show_timestamp  = false;

    if (isVerbose) {
        logCfg.min_level = pmake::logging::Level::DEBUG;
        pmake::logging::setVerbose(true);
    } else if (isQuiet) {
        logCfg.min_level = pmake::logging::Level::ERR;
    } else {
        logCfg.min_level = pmake::logging::Level::INFO;
    }
    pmake::logging::init(logCfg);

    QString config   = parser.value(configOption);
    QString platform = parser.value(platformOption);

    if (config.isEmpty() && command == "build" && args.size() > 1) {
        QString possibleConfig = args.at(1);
        if (possibleConfig == "debug" || possibleConfig == "release") {
            config = possibleConfig;
            if (args.size() > 2) target = args.at(2);
        } else {
            target = possibleConfig;
        }
    }

    // ---------- ПАРСИНГ ----------
    QFuture<std::unique_ptr<State>> future = QtConcurrent::run([&]() {
        pmake::parser::Parser parserImpl;
        try {
            return parserImpl.parse(buildFile, config, platform);
        } catch (const std::exception& e) {
            LOG_ERROR(QString("Parser thread exception: %1").arg(e.what()));
            return std::unique_ptr<State>(nullptr);
        } catch (...) {
            LOG_ERROR("Parser thread unknown exception");
            return std::unique_ptr<State>(nullptr);
        }
    });

    QFutureWatcher<std::unique_ptr<State>> watcher;
    watcher.setFuture(future);

    if (!future.isFinished()) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(60000);

        bool timedOut = false;
        QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
            timedOut = true;
            loop.quit();
        });
        QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, [&]() {
            timer.stop();
            loop.quit();
        });

        loop.exec();

        if (timedOut && !future.isFinished()) {
            LOG_ERROR("Parsing timed out after 60 seconds");
            _Exit(1);
        }
    }

    auto state = future.takeResult();
    if (!state) {
        LOG_ERROR("Failed to parse build file");
        return 1;
    }

    // ---------- DEFAULT-ЦЕЛЬ ----------
    if (target.isEmpty() && command == "build") {
        bool hasAll = false;
        for (const auto& build : state->builds()) {
            if (!build.outputs.isEmpty() && build.outputs[0] == "all") {
                hasAll = true;
                break;
            }
        }
        if (hasAll) target = "all";
        else if (!state->defaultTargets().isEmpty()) target = state->defaultTargets().first();
    }

    // ---------- КЭШ ----------
    bool useCache = true;
    auto cacheVar = state->getGlobal("cache");
    if (cacheVar.has_value() && cacheVar->toLower() == "false") {
        useCache = false;
    }

    pmake::core::Cache cache;
    bool cacheValid = false;
    if (useCache) {
        QString cachePath = QDir::current().filePath("Project-cache.pmake");
        if (cache.load(cachePath)) {
            // Загружаем ВСЕГДА, чтобы isDirty мог сравнить старые хэши с новыми
            for (auto it = cache.fileStates().begin(); it != cache.fileStates().end(); ++it) {
                state->setFileState(it.key(), it.value());
            }
            state->setCommandHashes(cache.commandHashes());

            // Проверяем, не изменился ли build.pmake после сохранения кэша.
            qint64 buildMtime = pmake::util::getFileMtime(buildFile);
            qint64 cacheMtime = pmake::util::getFileMtime(cachePath);
            bool buildFileNewer = (buildMtime != -1 && cacheMtime != -1 && buildMtime > cacheMtime);

            if (cache.isValid(buildFile) && !buildFileNewer) {
                cacheValid = true;
                LOG_VERBOSE(QString("Cache: valid, %1 file states, %2 deps, %3 command hashes")
                            .arg(cache.fileStates().size())
                            .arg(pmake::deps::DepsLog::instance().entries().size())
                            .arg(cache.commandHashes().size()));
            } else {
                LOG_VERBOSE("Cache: invalid (build file changed)");
                // Очищаем только DepsLog (зависимости пересканируем), 
                // но fileStates и commandHashes остаются в памяти для сравнения!
                pmake::deps::DepsLog::instance().clear();
            }
        } else {
            LOG_VERBOSE("Cache: not found");
        }
    }

    // Передаем статус кэша в движок. 
    state->setCacheValid(cacheValid);

    // ---------- DEPFILES (если кэш невалиден) ----------
    if (!cacheValid) {
        LOG_VERBOSE("Re-scanning depfiles");
        for (const auto& build : state->builds()) {
            const Rule* rule = state->getRule(build.rule_name);
            if (!rule || rule->name == "phony" || rule->generator || rule->depfile.isEmpty())
                continue;

            for (const QString& out : build.outputs) {
                QHash<QString, QString> specialVars;
                specialVars["out"] = out;
                specialVars["in"]  = build.inputs.join(' ');

                QString depfilePath = state->expandVarsWithSpecial(rule->depfile,
                                                                   build.scope.data(),
                                                                   specialVars);
                QVector<QString> inputs = pmake::deps::DepParser::parse(depfilePath);
                if (!inputs.isEmpty()) {
                    pmake::deps::DepsLog::instance().record(out, inputs);
                }
            }
        }
    }

    // ---------- СОХРАНЕНИЕ КЭША ПОСЛЕ ПАРСИНГА ----------
    if (useCache && !dryRun) {
        cache.store(*state, buildFile);
        QString cachePath = QDir::current().filePath("Project-cache.pmake");
        if (!cache.save(cachePath)) {
            LOG_WARN("Failed to save cache");
        } else {
            LOG_VERBOSE("Cache: saved");
        }
    }

    // ---------- CLEAN ----------
    if (command == "clean") {
        int removed = 0;
        for (const auto& build : state->builds()) {
            const Rule* rule = state->getRule(build.rule_name);
            if (rule && rule->generator) continue;
            for (const QString& out : build.outputs) {
                if (QFile::remove(out)) ++removed;
            }
        }
        LOG_INFO(QString("Cleaned %1 files").arg(removed));
        return 0;
    }

    if (command != "build") {
        LOG_ERROR(QString("Unknown command: %1").arg(command));
        return 1;
    }

    // ---------- ЗАПУСК ----------
    pmake::core::Engine engine(*state, jobs, dryRun);
    pmake::ui::ConsoleUI ui(isQuiet, useColor);

    QObject::connect(&engine, &pmake::core::Engine::taskStarted,
                     &ui, &pmake::ui::ConsoleUI::onTaskStarted, Qt::DirectConnection);
    QObject::connect(&engine, &pmake::core::Engine::taskFinished,
                     &ui, &pmake::ui::ConsoleUI::onTaskFinished, Qt::DirectConnection);
    QObject::connect(&engine, &pmake::core::Engine::buildFinished,
                     &ui, &pmake::ui::ConsoleUI::onBuildFinished, Qt::DirectConnection);

    bool result = engine.run(target);

    // ---------- ОБНОВЛЕНИЕ КЭША ПОСЛЕ СБОРКИ ----------
    if (!dryRun && useCache) {
        engine.updateFileStates();
        cache.store(*state, buildFile);
        QString cachePath = QDir::current().filePath("Project-cache.pmake");
        if (!cache.save(cachePath)) {
            LOG_WARN("Failed to save cache after build");
        }
    }

    return result ? 0 : 1;
}