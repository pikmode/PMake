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
#include <QMutexLocker>

namespace pmake::core {

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