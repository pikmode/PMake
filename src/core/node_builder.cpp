#include "core/node.h"
#include "core/graph.h"
#include "executor/runner.h"
#include "deps/dep_parser.h"
#include "deps/deps_log.h"
#include "util/filesystem.h"
#include "util/logging.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QMutexLocker>
#include <QSet>

namespace pmake::core {

static QString getWorkingDirectory(const Build* build, const State* state) {
    if (build && build->scope) {
        auto wd = build->scope->get("workdir");
        if (wd.has_value() && !wd->isEmpty()) return QDir::cleanPath(*wd);
    }
    if (state) {
        auto wd = state->getGlobal("workdir");
        if (wd.has_value() && !wd->isEmpty()) return QDir::cleanPath(*wd);
    }
    if (build && !build->outputs.isEmpty()) {
        QFileInfo fi(build->outputs.first());
        if (fi.absolutePath() != ".") return QDir::cleanPath(fi.absolutePath());
    }
    return QDir::currentPath();
}

static bool depsListsEqual(const QVector<QString>& a, const QVector<QString>& b) {
    if (a.size() != b.size()) return false;
    QSet<QString> setA(a.begin(), a.end());
    QSet<QString> setB(b.begin(), b.end());
    return setA == setB;
}

int Node::build() {
    if (!build_ || !rule_) return 0;
    if (isPhony()) return 0;

    // Dry-run: прогресс уже показан через Engine::taskStarted.
    // Здесь только обновляем mtime, чтобы downstream корректно считал dirty.
    if (dryRun_) {
        refreshOutputMtime();
        return 0;
    }

    {
        QMutexLocker locker(&buildMutex_);
        if (isBuilding_ || buildSuccess_) return 0;
        isBuilding_ = true;
    }

    struct BuildGuard {
        Node* node;
        bool success = false;
        BuildGuard(Node* n) : node(n) {}
        ~BuildGuard() {
            QMutexLocker locker(&node->buildMutex_);
            node->isBuilding_ = false;
            if (success) node->buildSuccess_ = true;
        }
    } guard(this);

    QHash<QString, QString> specialVars = getSpecialVars();
    QString command = rule_->command;
    if (state_) {
        command = state_->expandVarsWithSpecial(command, build_->scope.data(), specialVars);
    }
    if (command.trimmed().isEmpty()) {
        LOG_ERROR(QString("Empty command for node '%1'").arg(name_));
        return 1;
    }

    for (const QString& out : build_->outputs) {
        QFileInfo fi(out);
        if (!fi.absolutePath().isEmpty())
            QDir().mkpath(fi.absolutePath());
    }

    // ---------- RESPONSE-FILE ----------
    QString rspFilePath;
    if (!rule_->rspfile.isEmpty()) {
        rspFilePath = state_->expandVarsWithSpecial(rule_->rspfile, build_->scope.data(), specialVars);
        QString rspContent = state_->expandVarsWithSpecial(rule_->rspfile_content, build_->scope.data(), specialVars);
        if (!util::writeFile(rspFilePath, rspContent.toUtf8())) {
            LOG_ERROR("Failed to write response file: " + rspFilePath);
            return 1;
        }
        specialVars["rspfile"] = rspFilePath;
        command = state_->expandVarsWithSpecial(rule_->command, build_->scope.data(), specialVars);
    }

    QHash<QString, qint64> beforeMtimes = getOutputMtimesBefore();

    QStringList args = QProcess::splitCommand(command);
    if (args.isEmpty()) {
        LOG_ERROR(QString("Failed to split command for '%1'").arg(name_));
        return 1;
    }
    QString program = args.takeFirst();
    QString workDir = getWorkingDirectory(build_, state_);

    // -v: показать реальную команду под [N/M]. Идёт в stderr, чтобы не
    // засорять stdout (который может перенаправляться в CI-лог).
    if (pmake::logging::isVerbose()) {
        LOG_VERBOSE(QString("  %1").arg(command));
    }

    executor::Command cmd;
    cmd.program = program;
    cmd.arguments = args;
    cmd.workingDirectory = workDir;
    cmd.captureOutput = true;

    executor::Result res = executor::Runner::runSync(cmd);
    if (res.exitCode != 0) {
        LOG_ERROR(QString("Build failed for '%1' (exit %2)").arg(name_).arg(res.exitCode));
        if (!res.stderrOutput.isEmpty()) LOG_ERROR("STDERR: " + res.stderrOutput);
        return res.exitCode;
    }

    if (!rspFilePath.isEmpty() && util::fileExists(rspFilePath)) {
        QFile::remove(rspFilePath);
    }

    for (const QString& out : build_->outputs) {
        if (!util::fileExists(out)) {
            LOG_ERROR(QString("Output file not created: %1").arg(out));
            return 1;
        }
    }

    refreshOutputMtime();
    guard.success = true;

    bool outputChanged = hasOutputChanged(beforeMtimes);
    if (rule_->restat && !outputChanged) {
        LOG_VERBOSE(QString("Restat: output '%1' unchanged, skipping dependents").arg(name_));
        return 2;
    }

    // ---- Обработка depfile ----
    if (rule_ && !rule_->depfile.isEmpty()) {
        QString depfilePath = rule_->depfile;
        depfilePath = state_->expandVarsWithSpecial(depfilePath, build_->scope.data(), specialVars);
        QVector<QString> inputs = deps::DepParser::parse(depfilePath);
        if (!inputs.isEmpty()) {
            for (const QString& out : build_->outputs) {
                auto existingInputs = deps::DepsLog::instance().inputsFor(out);
                if (!depsListsEqual(existingInputs, inputs)) {
                    deps::DepsLog::instance().record(out, inputs);
                } else {
                    LOG_VERBOSE("Dependencies for " + out + " already up-to-date, skipping");
                }
            }
        }
    }

    return 0;
}

} // namespace pmake::core