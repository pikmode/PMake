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

} // namespace pmake::core