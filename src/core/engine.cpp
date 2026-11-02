#include "core/engine.h"
#include "core/node.h"
#include "util/logging.h"
#include "util/filesystem.h"
#include "deps/deps_log.h"
#include <QThread>
#include <QEventLoop>
#include <QDir>
#include <QMutexLocker>

namespace pmake::core {

// ===========================================================================
// Конструктор / деструктор
// ===========================================================================
Engine::Engine(State& state, int maxJobs, bool dryRun, QObject* parent)
    : QObject(parent)
    , state_(state)
    , graph_(state, dryRun)
    , maxJobs_(maxJobs > 0 ? maxJobs : 1)
    , dryRun_(dryRun) {

    pool_.setMaxThreadCount(maxJobs_);

    { PoolCounter pc; pc.maxJobs = maxJobs_; poolCounters_["default"] = pc; }
    { PoolCounter pc; pc.maxJobs = 1; poolCounters_["console"] = pc; }
    for (const auto& pool : state_.pools()) {
        PoolCounter pc;
        pc.maxJobs = pool.depth > 0 ? pool.depth : 1;
        poolCounters_[pool.name] = pc;
    }

    graphBuildFailed_ = !graph_.build();
    graph_.addDepsFromLog(deps::DepsLog::instance());

    qRegisterMetaType<pmake::core::Node*>("pmake::core::Node*");

    QObject::connect(this, &Engine::nodeDoneInternal,
                     this, &Engine::handleNodeDone,
                     Qt::QueuedConnection);
}

Engine::~Engine() {
    if (watchdog_) {
        watchdog_->stop();
        delete watchdog_;
        watchdog_ = nullptr;
    }
    cleanupPlan();
    pool_.waitForDone();
}

// ===========================================================================
// Публичный run()
// ===========================================================================
bool Engine::run(const QString& target) {
    if (graphBuildFailed_) {
        emit buildError("Graph build failed — see errors above");
        emit buildFinished(false, 0.0);
        return false;
    }

    QEventLoop loop;
    bool success = false;
    bool gotSignal = false;

    auto conn = QObject::connect(this, &Engine::buildFinished, &loop,
        [&](bool ok, double) {
            success = ok;
            gotSignal = true;
            loop.quit();
        });

    runInternal(target);

    if (!gotSignal && !finished_) {
        loop.exec();
    }

    QObject::disconnect(conn);
    return success;
}

// ===========================================================================
// runInternal
// ===========================================================================
void Engine::runInternal(const QString& target) {
    emit buildStarted();
    initializeFileStates();
    buildTimer_.start();

    completedTasks_ = 0;
    runningTasks_ = 0;
    pendingTasks_ = 0;
    aborted_ = false;
    finished_ = false;

    QString targetName = target;
    if (targetName.isEmpty()) {
        bool hasAll = false;
        for (const auto& build : state_.builds()) {
            if (!build.outputs.isEmpty() && build.outputs[0] == "all") {
                hasAll = true;
                break;
            }
        }
        if (hasAll) targetName = "all";
        else if (!state_.defaultTargets().isEmpty()) {
            targetName = state_.defaultTargets().first();
        } else {
            QStringList candidates;
            for (const auto& build : state_.builds())
                for (const QString& out : build.outputs) candidates.append(out);
            if (!candidates.isEmpty()) targetName = candidates.last();
            else {
                emit buildError("No target specified and no builds");
                finished_ = true;
                emit buildFinished(false, 0.0);
                return;
            }
        }
    }

    QVector<Node*> order;
    try {
        order = graph_.topologicalSort(targetName);
    } catch (const std::exception& e) {
        emit buildError(QString("Graph error: ") + e.what());
        finished_ = true;
        emit buildFinished(false, buildTimer_.elapsed() / 1000.0);
        return;
    }
    if (order.isEmpty()) {
        emit buildError(QString("Target '%1' not found").arg(targetName));
        finished_ = true;
        emit buildFinished(false, buildTimer_.elapsed() / 1000.0);
        return;
    }

    auto dirtyNodes = collectDirtyNodes(order);
    if (dirtyNodes.isEmpty()) {
        LOG_INFO("All targets up to date");
        finished_ = true;
        emit buildFinished(true, buildTimer_.elapsed() / 1000.0);
        return;
    }

    buildPlan(dirtyNodes);
    if (totalTasks_ == 0) {
        LOG_INFO("Nothing to build");
        finished_ = true;
        emit buildFinished(true, buildTimer_.elapsed() / 1000.0);
        return;
    }

    emit buildProgress(0, totalTasks_);

    if (!watchdog_) {
        watchdog_ = new QTimer(this);
        watchdog_->setSingleShot(true);
        QObject::connect(watchdog_, &QTimer::timeout, this, [this]() {
            if (finished_) return;
            LOG_ERROR("Build timed out after 5 minutes");
            abortBuild("Build timed out");
        });
    }
    watchdog_->start(1800000);

    pumpReady();
}

// ===========================================================================
// File-states
// ===========================================================================
void Engine::initializeFileStates() {
    auto leaves = graph_.leafNodes();
    for (Node* leaf : leaves) {
        if (!state_.getFileState(leaf->name()).has_value()) {
            qint64 mtime = -1, size = 0;
            if (util::statFileFast(leaf->name(), mtime, size)) {
                FileState fs;
                fs.mtime = mtime;
                fs.size = size;
                state_.setFileState(leaf->name(), fs);
                util::FileCache::instance().setMtime(leaf->name(), mtime);
            }
        }
    }
}

void Engine::updateFileStates() {
    auto leaves = graph_.leafNodes();
    for (Node* leaf : leaves) {
        qint64 mtime = -1, size = 0;
        util::statFileFast(leaf->name(), mtime, size);
        FileState fs;
        fs.mtime = mtime;
        fs.size = size;
        state_.setFileState(leaf->name(), fs);
        util::FileCache::instance().setMtime(leaf->name(), mtime);
    }
}

// ===========================================================================
// Helper: имя пула для задачи
// ===========================================================================
QString Engine::poolNameFor(const Build* b) const {
    if (b && b->scope) {
        auto pv = b->scope->get("pool");
        if (pv.has_value() && !pv->isEmpty()) return *pv;
    }
    const Rule* rule = b ? state_.getRule(b->rule_name) : nullptr;
    if (rule && rule->pool) return "console";
    return "default";
}

} // namespace pmake::core