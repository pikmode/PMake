#include "core/node.h"
#include "util/filesystem.h"
#include "util/logging.h"
#include "util/string_utils.h"
#include "executor/runner.h"
#include "deps/dep_parser.h"
#include "deps/deps_log.h"
#include "core/graph.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace pmake::core {

Node::Node(const QString& name, const Build* build, const Rule* rule, const State* state, Graph* graph, bool dryRun)
    : name_(name), build_(build), rule_(rule), state_(state), graph_(graph), dryRun_(dryRun), finished_(false) {}

void Node::addDependency(Node* dep) {
    if (!dep) return;
    if (!deps_.contains(dep)) {
        deps_.append(dep);
        dep->addReverseDependency(this);
    }
}

void Node::addImplicitDependency(Node* dep) {
    if (!dep) return;
    if (!implicit_deps_.contains(dep)) {
        implicit_deps_.append(dep);
        dep->addReverseDependency(this);
    }
}

void Node::addOrderOnlyDependency(Node* dep) {
    if (!dep) return;
    if (!order_only_deps_.contains(dep)) {
        order_only_deps_.append(dep);
        dep->addReverseDependency(this);
    }
}

void Node::addReverseDependency(Node* dep) {
    if (!dep) return;
    if (!reverse_deps_.contains(dep)) {
        reverse_deps_.append(dep);
    }
}

qint64 Node::getFileMtime(const QString& path) {
    return util::getFileMtime(path);
}

bool Node::isDirty() {
    if (isPhony()) return false;
    if (!build_ || !rule_) return false;

    if (rule_->generator) {
        for (const QString& out : build_->outputs) {
            if (!util::fileExists(out)) return true;
        }
        return false;
    }

    bool anyExists = false;
    qint64 maxOut = -1;
    for (const QString& out : build_->outputs) {
        if (util::fileExists(out)) {
            anyExists = true;
            qint64 m = util::getFileMtime(out);
            if (m > maxOut) maxOut = m;
        } else {
            return true;
        }
    }
    if (!anyExists) return true;

    auto checkNode = [&](Node* dep) -> bool {
        if (dep->isLeaf()) {
            qint64 m = util::getFileMtime(dep->name());
            if (m > maxOut) return true;
        } else {
            qint64 m = dep->outputMtime();
            if (m > maxOut) return true;
        }
        return false;
    };

    for (Node* dep : deps_) {
        if (checkNode(dep)) return true;
    }
    for (Node* dep : implicit_deps_) {
        if (checkNode(dep)) return true;
    }

    auto depInputs = deps::DepsLog::instance().inputsFor(name_);
    if (!depInputs.isEmpty()) {
        qint64 maxDepMtime = -1;
        for (const QString& in : depInputs) {
            qint64 m = util::getFileMtime(in);
            if (m > maxDepMtime) maxDepMtime = m;
        }
        if (maxDepMtime > maxOut) return true;
    }

    cachedOutputMtime_ = maxOut;
    mtimeValid_ = true;
    return false;
}

QHash<QString, QString> Node::getSpecialVars() const {
    QHash<QString, QString> specialVars;
    if (!build_ || !rule_) return specialVars;

    auto quote = [](const QString& s) -> QString {
        return util::quoteForShell(s);
    };

    QStringList quotedOutputs;
    for (const QString& out : build_->outputs) quotedOutputs.append(quote(out));
    specialVars["out"] = quotedOutputs.join(' ');
    if (build_->outputs.isEmpty()) {
        LOG_WARN(QString("Node '%1' has empty outputs!").arg(name_));
    }

    QStringList quotedInputs;
    for (const QString& in : build_->inputs) quotedInputs.append(quote(in));
    specialVars["in"] = quotedInputs.join(' ');
    specialVars["in_newline"] = build_->inputs.join('\n');
    if (build_->inputs.isEmpty()) {
        LOG_WARN(QString("Node '%1' has empty inputs!").arg(name_));
    }

    if (build_->scope) {
        for (const auto& key : build_->scope->locals().keys()) {
            if (!specialVars.contains(key))
                specialVars[key] = build_->scope->locals().value(key);
        }
    }
    if (state_) {
        for (const auto& key : state_->globals().keys()) {
            if (!specialVars.contains(key))
                specialVars[key] = state_->globals().value(key);
        }
    }
    return specialVars;
}

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

QHash<QString, qint64> Node::getOutputMtimesBefore() const {
    QHash<QString, qint64> before;
    for (const QString& out : build_->outputs) {
        before[out] = util::getFileMtime(out);
    }
    return before;
}

bool Node::hasOutputChanged(const QHash<QString, qint64>& before) const {
    for (const QString& out : build_->outputs) {
        qint64 now = util::getFileMtime(out);
        if (before.value(out) != now) return true;
    }
    return false;
}

// Вспомогательная функция сравнения списков зависимостей
static bool depsListsEqual(const QVector<QString>& a, const QVector<QString>& b) {
    if (a.size() != b.size()) return false;
    QSet<QString> setA(a.begin(), a.end());
    QSet<QString> setB(b.begin(), b.end());
    return setA == setB;
}

int Node::build() {
    if (!build_ || !rule_) return 0;
    if (isPhony()) return 0;

    // ---- Режим dry-run ----
    if (dryRun_) {
        QHash<QString, QString> specialVars = getSpecialVars();
        QString text = rule_->description.isEmpty() ? rule_->command : rule_->description;
        if (state_) {
            text = state_->expandVarsWithSpecial(text, build_->scope.data(), specialVars);
        }
        // Не выводим ничего, т.к. вывод делает ConsoleUI
        refreshOutputMtime(); // фиктивное обновление
        return 0;
    }

    // ---- Реальный режим ----
    {
        QMutexLocker locker(&buildMutex_);
        if (isBuilding_ || buildSuccess_) return 0;
        isBuilding_ = true;
    }

    struct BuildGuard {
        Node* node;
        bool success = false;
        bool changed = false;
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

    // ---------- СОЗДАНИЕ RESPONSE-ФАЙЛА ----------
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

    // ---- Обработка depfile с защитой от дублирования ----
    if (rule_ && !rule_->depfile.isEmpty()) {
        QString depfilePath = rule_->depfile;
        depfilePath = state_->expandVarsWithSpecial(depfilePath, build_->scope.data(), specialVars);
        QVector<QString> inputs = deps::DepParser::parse(depfilePath);
        if (!inputs.isEmpty()) {
            for (const QString& out : build_->outputs) {
                auto existingInputs = deps::DepsLog::instance().inputsFor(out);
                if (!depsListsEqual(existingInputs, inputs)) {
                    deps::DepsLog::instance().record(out, inputs);
                    if (graph_) {
                        graph_->addDepsForOutput(out, inputs);
                    }
                } else {
                    LOG_VERBOSE("Dependencies for " + out + " already up-to-date, skipping");
                }
            }
        }
    }

    return 0;
}

void Node::refreshOutputMtime() {
    qint64 maxOut = -1;
    for (const QString& out : build_->outputs) {
        qint64 m = util::getFileMtime(out);
        if (m > maxOut) maxOut = m;
    }
    cachedOutputMtime_ = maxOut;
    mtimeValid_ = true;
}

} // namespace pmake::core