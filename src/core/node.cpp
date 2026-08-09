#include "core/node.h"
#include "util/filesystem.h"
#include "util/logging.h"
#include "util/string_utils.h"
#include "deps/deps_log.h"
#include <QDir>
#include <QFileInfo>

namespace pmake::core {

// Конструктор
Node::Node(const QString& name, const Build* build, const Rule* rule, const State* state, Graph* graph, bool dryRun)
    : name_(name), build_(build), rule_(rule), state_(state), graph_(graph), dryRun_(dryRun), finished_(false) {}

// Управление зависимостями
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

// Вспомогательные методы
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