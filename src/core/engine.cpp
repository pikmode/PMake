#include "core/engine.h"
#include "util/logging.h"
#include "util/filesystem.h"
#include "executor/runner.h"
#include "deps/dep_parser.h"
#include "deps/deps_log.h"
#include <QThread>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <QSet>
#include <QDir>

namespace pmake::core {

Engine::Engine(State& state, int maxJobs, bool dryRun, QObject* parent)
    : QObject(parent), state_(state), graph_(state, dryRun), maxJobs_(maxJobs), dryRun_(dryRun) {
    for (const auto& pool : state_.pools()) {
        PoolCounter pc;
        pc.maxJobs = pool.depth;
        pc.active = 0;
        poolCounters_[pool.name] = pc;
    }
    if (!poolCounters_.contains("default")) {
        PoolCounter pc;
        pc.maxJobs = maxJobs;
        pc.active = 0;
        poolCounters_["default"] = pc;
    }
    if (!poolCounters_.contains("console")) {
        PoolCounter pc;
        pc.maxJobs = 1;
        pc.active = 0;
        poolCounters_["console"] = pc;
    }
    graph_.build();
    graph_.addDepsFromLog(deps::DepsLog::instance());
}

Engine::~Engine() { pool_.waitForDone(); }

bool Engine::run(const QString& target) {
    QEventLoop loop;
    bool success = false;
    bool finished = false;

    QObject::connect(this, &Engine::buildFinished,
                     [&](bool ok, double) {
                         success = ok;
                         finished = true;
                         loop.quit();
                     });
    QObject::connect(this, &Engine::buildError,
                     [&](const QString&) {
                         success = false;
                         finished = true;
                         loop.quit();
                     });

    runInternal(target);

    if (!finished) {
        loop.exec();
    }

    return success;
}

void Engine::runInternal(const QString& target) {
    emit buildStarted();
    initializeFileStates();
    buildTimer_.start();

    QString targetName = target;
    if (targetName.isEmpty()) {
        bool hasAll = false;
        for (const auto& build : state_.builds()) {
            if (!build.outputs.isEmpty() && build.outputs[0] == "all") {
                hasAll = true;
                break;
            }
        }
        if (hasAll) {
            targetName = "all";
        } else if (!state_.defaultTargets().isEmpty()) {
            targetName = state_.defaultTargets().first();
        } else {
            QStringList candidates;
            for (const auto& build : state_.builds()) {
                for (const QString& out : build.outputs) candidates.append(out);
            }
            if (!candidates.isEmpty()) targetName = candidates.last();
            else {
                emit buildError("No target specified and no builds");
                emit buildFinished(false, buildTimer_.elapsed() / 1000.0);
                return;
            }
        }
    }

    QVector<Node*> order;
    try {
        order = graph_.topologicalSort(targetName);
    } catch (const std::exception& e) {
        emit buildError(QString("Graph error: ") + e.what());
        emit buildFinished(false, buildTimer_.elapsed() / 1000.0);
        return;
    }
    if (order.isEmpty()) {
        emit buildError(QString("Target '%1' not found").arg(targetName));
        emit buildFinished(false, buildTimer_.elapsed() / 1000.0);
        return;
    }

    auto dirtyNodes = collectDirtyNodes(order);
    if (dirtyNodes.isEmpty()) {
        LOG_INFO("All targets up to date");
        emit buildFinished(true, buildTimer_.elapsed() / 1000.0);
        return;
    }

    buildToNodes_.clear();
    for (Node* n : dirtyNodes) {
        const Build* b = n->getBuild();
        if (b) buildToNodes_[b].append(n);
    }

    QSet<const Build*> uniqueBuilds;
    for (Node* n : dirtyNodes) {
        const Build* build = n->getBuild();
        if (build && isRealBuild(build)) uniqueBuilds.insert(build);
    }
    totalTasks_ = uniqueBuilds.size();
    if (totalTasks_ == 0) {
        emit buildFinished(true, buildTimer_.elapsed() / 1000.0);
        return;
    }
    emit buildProgress(0, totalTasks_);

    tasksCompleted_ = 0;
    buildFailed_ = false;
    activeBuilds_.clear();
    completedBuilds_.clear();

    {
        QMutexLocker locker(&scheduleMutex_);
        dirtySet_ = QSet<Node*>(dirtyNodes.begin(), dirtyNodes.end());
        for (Node* n : dirtyNodes) {
            pending_[n] = {0, 0};
            regularDependents_[n] = QVector<Node*>();
            orderOnlyDependents_[n] = QVector<Node*>();
        }
        for (Node* n : dirtyNodes) {
            auto addDep = [&](Node* dep) {
                if (dirtySet_.contains(dep)) {
                    pending_[n].regular++;
                    regularDependents_[dep].append(n);
                }
            };
            for (Node* dep : n->dependencies()) addDep(dep);
            for (Node* dep : n->implicitDeps()) addDep(dep);
            for (Node* dep : n->orderOnlyDeps()) {
                if (dirtySet_.contains(dep)) {
                    pending_[n].orderOnly++;
                    orderOnlyDependents_[dep].append(n);
                }
            }
        }
    }

    for (Node* n : dirtyNodes) {
        if (pending_[n].regular == 0 && pending_[n].orderOnly == 0) {
            tryStartNode(n, false);
        }
    }

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [this]() {
        LOG_ERROR("Build timed out after 5 minutes");
        emit buildError("Build timed out");
        emit buildFinished(false, buildTimer_.elapsed() / 1000.0);
    });
    watchdog.start(300000);
}

bool Engine::isRealBuild(const Build* build) const {
    if (!build) return false;
    const Rule* rule = state_.getRule(build->rule_name);
    if (!rule) return false;
    if (rule->name == "phony") return false;
    if (rule->command.trimmed().isEmpty()) return false;
    return true;
}

void Engine::initializeFileStates() {
    auto leaves = graph_.leafNodes();
    for (Node* leaf : leaves) {
        if (!state_.getFileState(leaf->name()).has_value()) {
            qint64 mtime = util::getFileMtime(leaf->name());
            if (mtime != -1) {
                FileState fs;
                fs.mtime = mtime;
                QFileInfo fi(leaf->name());
                fs.size = fi.size();
                state_.setFileState(leaf->name(), fs);
            }
        }
    }
}

void Engine::updateFileStates() {
    auto leaves = graph_.leafNodes();
    for (Node* leaf : leaves) {
        qint64 mtime = util::getFileMtime(leaf->name());
        FileState fs;
        fs.mtime = mtime;
        QFileInfo fi(leaf->name());
        fs.size = fi.size();
        state_.setFileState(leaf->name(), fs);
    }
}

QVector<Node*> Engine::collectDirtyNodes(const QVector<Node*>& order) {
    QSet<Node*> reachable;
    for (Node* n : order) reachable.insert(n);

    QSet<Node*> dirtySet;

    for (Node* node : order) {
        if (node->isLeaf()) {
            qint64 currentMtime = util::getFileMtime(node->name());
            auto cached = state_.getFileState(node->name());
            if (!cached.has_value() || cached->mtime != currentMtime) {
                for (Node* consumer : node->reverseDeps()) {
                    if (reachable.contains(consumer)) {
                        dirtySet.insert(consumer);
                    }
                }
            }
            continue;
        }

        if (node->isDirty()) {
            dirtySet.insert(node);
        }
    }

    QSet<Node*> expanded;
    for (Node* n : dirtySet) {
        if (reachable.contains(n)) {
            expanded.insert(n);
            for (Node* consumer : n->reverseDeps()) {
                if (reachable.contains(consumer) && !dirtySet.contains(consumer)) {
                    expanded.insert(consumer);
                }
            }
        }
    }

    QVector<Node*> result;
    for (Node* n : expanded) {
        if (n->getBuild() != nullptr) {
            result.append(n);
        }
    }
    return result;
}

void Engine::tryStartNode(Node* node, bool fromPool) {
    if (!node || node->isFinished()) return;

    const Build* build = node->getBuild();
    if (!isRealBuild(build)) {
        node->setFinished(true);
        releaseDependencies(node);
        return;
    }

    {
        QMutexLocker locker(&buildMutex_);
        if (completedBuilds_.contains(build) || activeBuilds_.contains(build)) {
            return;
        }
    }

    const Rule* rule = node->rule();
    QString poolName = "default";
    if (build && build->scope) {
        auto poolVar = build->scope->get("pool");
        if (poolVar.has_value() && poolVar->toLower() == "console") poolName = "console";
    }
    if (!poolCounters_.contains(poolName)) poolName = "default";

    if (!fromPool) {
        QMutexLocker locker(&poolMutex_);
        PoolCounter& pc = poolCounters_[poolName];
        if (pc.active >= pc.maxJobs) {
            if (!pc.waitingSet.contains(node)) {
                pc.waiting.enqueue(node);
                pc.waitingSet.insert(node);
            }
            return;
        }
        pc.active++;
    }

    {
        QMutexLocker locker(&buildMutex_);
        if (activeBuilds_.contains(build) || completedBuilds_.contains(build)) {
            if (!fromPool) {
                QMutexLocker poolLocker(&poolMutex_);
                PoolCounter& pc = poolCounters_[poolName];
                if (pc.active > 0) --pc.active;
            }
            return;
        }
        activeBuilds_.insert(build);
    }

    QString display = node->name();
    if (rule && !rule->description.isEmpty()) {
        QHash<QString, QString> specialVars = node->getSpecialVars();
        display = state_.expandVarsWithSpecial(rule->description, build ? build->scope.data() : nullptr, specialVars);
    }
    emit taskStarted(display, totalTasks_);

    pool_.start([this, node, poolName]() {
        QElapsedTimer timer;
        timer.start();
        int exitCode = 0;
        try {
            exitCode = node->build();
        } catch (const std::exception& e) {
            LOG_ERROR(QString("Exception in node->build(): %1").arg(e.what()));
            exitCode = 1;
        }
        double duration = timer.elapsed() / 1000.0;
        onNodeFinished(node, exitCode, duration);
    });
}

void Engine::releaseDependencies(Node* node) {
    QMutexLocker locker(&scheduleMutex_);
    if (regularDependents_.contains(node)) {
        for (Node* dependent : regularDependents_[node]) {
            auto it = pending_.find(dependent);
            if (it != pending_.end()) {
                it->regular--;
                if (it->regular == 0 && it->orderOnly == 0) {
                    tryStartNode(dependent, false);
                }
            }
        }
    }
    if (orderOnlyDependents_.contains(node)) {
        for (Node* dependent : orderOnlyDependents_[node]) {
            auto it = pending_.find(dependent);
            if (it != pending_.end()) {
                it->orderOnly--;
                if (it->regular == 0 && it->orderOnly == 0) {
                    tryStartNode(dependent, false);
                }
            }
        }
    }
}

void Engine::releasePoolSlot(Node* node) {
    const Build* build = node->getBuild();
    QString poolName = "default";
    if (build && build->scope) {
        auto poolVar = build->scope->get("pool");
        if (poolVar.has_value() && poolVar->toLower() == "console")
            poolName = "console";
    }
    if (!poolCounters_.contains(poolName))
        poolName = "default";

    QMutexLocker locker(&poolMutex_);
    PoolCounter& pc = poolCounters_[poolName];
    if (pc.active > 0) --pc.active;

    while (!pc.waiting.isEmpty() && pc.active < pc.maxJobs) {
        Node* waitingNode = pc.waiting.dequeue();
        pc.waitingSet.remove(waitingNode);
        ++pc.active;
        locker.unlock();
        tryStartNode(waitingNode, true);
        locker.relock();
    }
}

void Engine::onNodeFinished(Node* node, int exitCode, double duration) {
    releasePoolSlot(node);

    if (exitCode != 0 && exitCode != 2) {
        buildFailed_ = true;
        emit buildError("Build failed");
        emit buildFinished(false, buildTimer_.elapsed() / 1000.0);
        return;
    }

    const Build* build = node->getBuild();
    bool isReal = isRealBuild(build);

    if (isReal && build) {
        if (!completedBuilds_.contains(build)) {
            completedBuilds_.insert(build);
            auto nodes = buildToNodes_.value(build);
            for (Node* n : nodes) {
                if (!n->isFinished()) {
                    n->setFinished(true);
                    releaseDependencies(n);
                }
            }
            // Исправлено: учитываем exitCode 0 и 2 как успех
            if (exitCode == 0 || exitCode == 2) {
                int completed = tasksCompleted_.fetchAndAddOrdered(1) + 1;
                emit buildProgress(completed, totalTasks_);
                emit nodeBuilt(node->name(), exitCode, duration);
            }
        }
    } else {
        node->setFinished(true);
        releaseDependencies(node);
    }

    if (buildFailed_) {
        emit buildError("Build failed");
        emit buildFinished(false, buildTimer_.elapsed() / 1000.0);
        return;
    }

    if (tasksCompleted_ == totalTasks_) {
        emit buildFinished(true, buildTimer_.elapsed() / 1000.0);
        return;
    }

    bool allFinished = true;
    for (Node* n : dirtySet_) {
        if (!n->isFinished()) {
            allFinished = false;
            break;
        }
    }
    if (allFinished) {
        emit buildFinished(true, buildTimer_.elapsed() / 1000.0);
        return;
    }

    bool hasActive = false;
    for (const auto& pc : poolCounters_) {
        if (pc.active > 0) { hasActive = true; break; }
    }
    if (!hasActive && pool_.activeThreadCount() == 0 && !buildFailed_) {
        LOG_ERROR("Build stalled: no active tasks but unfinished nodes remain");
        emit buildError("Build stalled");
        emit buildFinished(false, buildTimer_.elapsed() / 1000.0);
    }
}

} // namespace pmake::core