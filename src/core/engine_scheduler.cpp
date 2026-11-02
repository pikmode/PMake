#include "core/engine.h"
#include "core/node.h"
#include "util/logging.h"
#include "util/filesystem.h"
#include "deps/deps_log.h"
#include <QElapsedTimer>
#include <QMutexLocker>
#include <functional>
#include <algorithm>

namespace pmake::core {

// ===========================================================================
// buildPlan: сгруппировать dirty nodes -> SchedTask по Build
//
// FIX (детерминизм): раньше итерировали QHash<const Build*, ...> —
// порядок случайный. На первом билде (все таски ready) `[1/N]` мог быть
// `Building CXX object` вместо `Automatic MOC`. В ninja порядок детерминирован
// (FIFO из build.ninja). Теперь сортируем по rule_name:
//
//   CUSTOM_COMMAND__autogen_core   (autogen core)     ← [1]
//   CUSTOM_COMMAND__autogen_exe    (autogen exe)      ← [2]
//   CUSTOM_COMMAND__rcc            (rcc)              ← [3]
//   CXX_COMPILER__pmake_core       (33 core .obj)     ← [4..36]
//   CXX_COMPILER__pmake            (3 exe .obj)       ← [37..39]
//   CXX_STATIC_LIBRARY_LINKER__... (libpmake_core.a)  ← [40]
//   CXX_EXECUTABLE_LINKER__...     (pmake.exe)        ← [41]
//
// "CUSTOM" < "CXX_C" < "CXX_E" < "CXX_S" — лексикографически autogen/rcc
// всегда раньше компиляции. Совпадает с порядком ninja.
// ===========================================================================
void Engine::buildPlan(const QVector<Node*>& dirtyNodesIn) {
    QMutexLocker lock(&planMutex_);

    // 1. Сортируем dirtyNodes стабильно по rule_name.
    QVector<Node*> dirtyNodes = dirtyNodesIn;
    std::stable_sort(dirtyNodes.begin(), dirtyNodes.end(),
        [](Node* a, Node* b) {
            const Build* ba = a ? a->getBuild() : nullptr;
            const Build* bb = b ? b->getBuild() : nullptr;
            if (!ba && !bb) return false;
            if (!ba) return false;   // leaf после build
            if (!bb) return true;
            // Primary: rule_name (autogen < compile < link)
            if (ba->rule_name != bb->rule_name)
                return ba->rule_name < bb->rule_name;
            // Secondary: имя ноды (стабильный runnerNode → стабильный
            // display name в `[N/M] Building CXX object ...`)
            return a->name() < b->name();
        });

    // 2. Группируем по Build, сохраняя порядок первого появления.
    QHash<const Build*, QVector<Node*>> buildToNodes;
    QVector<const Build*> buildOrder;

    for (Node* n : dirtyNodes) {
        if (!n) continue;
        const Build* b = n->getBuild();
        if (!b) continue;
        auto it = buildToNodes.find(b);
        if (it == buildToNodes.end()) {
            buildToNodes.insert(b, QVector<Node*>());
            buildOrder.append(b);
            it = buildToNodes.find(b);
        }
        it.value().append(n);
    }

    // 3. Создаём SchedTask в детерминированном порядке buildOrder.
    for (const Build* b : buildOrder) {
        const QVector<Node*>& nodes = buildToNodes[b];

        SchedTask* t = new SchedTask();
        t->build = b;
        t->nodes = nodes;
        t->runnerNode = nodes.isEmpty() ? nullptr : nodes.first();
        t->isReal = isRealBuild(b);
        t->poolName = poolNameFor(b);
        if (!poolCounters_.contains(t->poolName)) t->poolName = "default";

        tasks_.insert(b, t);
        for (Node* n : nodes) nodeToTask_.insert(n, t);
    }

    // 4. Зависимости между тасками. Разворачиваем order-only phony
    //    рекурсивно (CMake-паттерн: cmake_object_order_depends_target_X).
    for (SchedTask* t : tasks_) {
        QSet<SchedTask*> taskDeps;
        auto scan = [&](Node* node) {
            QSet<Node*> visiting;
            std::function<void(Node*)> visit = [&](Node* dep) {
                if (!dep) return;
                auto tit = nodeToTask_.constFind(dep);
                if (tit != nodeToTask_.constEnd()) {
                    SchedTask* dt = tit.value();
                    if (dt && dt != t) taskDeps.insert(dt);
                    return;
                }
                // dep не в nodeToTask_ — либо up-to-date real, либо phony.
                // Если phony — развернуть рекурсивно.
                if (dep->isPhony() && !visiting.contains(dep)) {
                    visiting.insert(dep);
                    for (Node* d : dep->dependencies())   visit(d);
                    for (Node* d : dep->implicitDeps())   visit(d);
                    for (Node* d : dep->orderOnlyDeps())  visit(d);
                }
            };
            for (Node* d : node->dependencies())   visit(d);
            for (Node* d : node->implicitDeps())   visit(d);
            for (Node* d : node->orderOnlyDeps())  visit(d);
        };
        for (Node* n : t->nodes) scan(n);
        t->pendingDeps = taskDeps.size();
        for (SchedTask* dt : taskDeps) dt->dependents.append(t);
    }

    // 5. Считаем totalTasks_ (только real) и pendingTasks_ (включая phony).
    totalTasks_ = 0;
    for (SchedTask* t : tasks_) {
        pendingTasks_++;
        if (t->isReal) totalTasks_++;
    }

    // 6. Заполняем ready queue в порядке buildOrder (детерминированно).
    //    Раньше итерировали QHash<const Build*, SchedTask*> — случайный
    //    порядок. Теперь — по buildOrder (после stable_sort по rule_name).
    for (const Build* b : buildOrder) {
        SchedTask* t = tasks_.value(b, nullptr);
        if (!t) continue;
        if (t->pendingDeps == 0) {
            t->state = TaskState::Ready;
            readyQueue_.enqueue(t);
            readySet_.insert(t);
        }
    }

    LOG_DEBUG(QString("Plan: %1 real tasks (total incl. phony: %2), %3 ready")
              .arg(totalTasks_).arg(pendingTasks_).arg(readyQueue_.size()));
}

// ===========================================================================
// pumpReady
// ===========================================================================
void Engine::pumpReady() {
    if (finished_) return;

    QVector<SchedTask*> toStart;
    {
        QMutexLocker lock(&planMutex_);
        if (finished_ || aborted_) return;

        while (!readyQueue_.isEmpty()) {
            SchedTask* t = readyQueue_.head();
            if (!t) { readyQueue_.dequeue(); continue; }

            PoolCounter& pc = poolCounters_[t->poolName];
            if (pc.active >= pc.maxJobs) break;

            readyQueue_.dequeue();
            readySet_.remove(t);

            t->state = TaskState::Running;
            pc.active++;
            runningTasks_++;
            toStart.append(t);
        }
    }

    for (SchedTask* t : toStart) startTask(t);
    checkFinished();
}

// ===========================================================================
// startTask: phony — сразу Done, real — emit taskStarted с description
// + P2 runtime re-check для restat (skip если clean)
// ===========================================================================
void Engine::startTask(SchedTask* t) {
    if (!t || !t->runnerNode) return;

    if (!t->isReal) {
        emit nodeDoneInternal(t->runnerNode, 0, 0.0);
        return;
    }

    // P2: runtime re-check для restat.
    if (!dryRun_ && !t->runnerNode->isDirty()) {
        LOG_VERBOSE(QString("Skipping '%1' (clean at runtime)").arg(t->runnerNode->name()));
        emit nodeDoneInternal(t->runnerNode, 2, 0.0);
        return;
    }

    // stable display name (multi-output).
    QString display = (t->build && !t->build->outputs.isEmpty())
                      ? t->build->outputs.first()
                      : t->runnerNode->name();

    const Rule* rule = t->runnerNode->rule();
    if (rule && !rule->description.isEmpty()) {
        QHash<QString, QString> specialVars = t->runnerNode->getSpecialVars();
        display = state_.expandVarsWithSpecial(
            rule->description,
            t->build ? t->build->scope.data() : nullptr,
            specialVars);
    }
    emit taskStarted(display, totalTasks_);

    Node* runner = t->runnerNode;

    pool_.start([this, runner]() {
        QElapsedTimer timer;
        timer.start();
        int exitCode = 0;
        try {
            exitCode = runner->build();
        } catch (const std::exception& e) {
            LOG_ERROR(QString("Exception in Node::build(): %1").arg(e.what()));
            exitCode = 1;
        } catch (...) {
            LOG_ERROR("Unknown exception in Node::build()");
            exitCode = 1;
        }
        double dur = timer.elapsed() / 1000.0;
        emit nodeDoneInternal(runner, exitCode, dur);
    });
}

// ===========================================================================
// handleNodeDone
// ===========================================================================
void Engine::handleNodeDone(Node* node, int exitCode, double duration) {
    if (!node) return;

    QString nodeName = node->name();
    bool success = false;
    bool wasReal = false;
    QString errorMsg;
    bool shouldPump = false;
    QVector<Node*> affectedNodes;

    {
        QMutexLocker lock(&planMutex_);
        if (finished_) return;

        auto tit = nodeToTask_.find(node);
        if (tit == nodeToTask_.end()) return;
        SchedTask* t = tit.value();
        if (!t || t->state != TaskState::Running) return;

        if (t->build && !t->build->outputs.isEmpty()) {
            nodeName = t->build->outputs.first();
        }

        auto pit = poolCounters_.find(t->poolName);
        if (pit != poolCounters_.end() && pit->active > 0) pit->active--;

        runningTasks_--;
        pendingTasks_--;

        bool ok = (exitCode == 0 || exitCode == 2);
        success = ok;
        wasReal = t->isReal;

        if (!ok) {
            t->state = TaskState::Failed;
            errorMsg = QString("Build failed for '%1' (exit %2)").arg(nodeName).arg(exitCode);
            aborted_ = true;
        } else {
            t->state = TaskState::Done;
            if (t->isReal) completedTasks_++;
            affectedNodes = t->nodes;

            // exit 0 = пересобран → wasBuilt=true → downstream force dirty.
            // exit 2 = restat skip → wasBuilt остаётся false → mtime проверка.
            for (Node* n : t->nodes) {
                n->setFinished(true);
                if (exitCode == 0) n->setWasBuilt(true);
            }

            for (SchedTask* dt : t->dependents) {
                if (!dt || dt->state != TaskState::Want) continue;
                if (dt->pendingDeps > 0) dt->pendingDeps--;
                if (dt->pendingDeps == 0) {
                    dt->state = TaskState::Ready;
                    readyQueue_.enqueue(dt);
                    readySet_.insert(dt);
                }
            }
            shouldPump = true;
        }
    }

    if (success) {
        for (Node* n : affectedNodes) {
            const QString& out = n->name();
            auto inputs = deps::DepsLog::instance().inputsFor(out);
            if (!inputs.isEmpty()) graph_.addDepsForOutput(out, inputs);
        }
        if (wasReal) {
            emit nodeBuilt(nodeName, exitCode, duration);
            emit taskFinished(nodeName, true, duration, QString());
            emit buildProgress(completedTasks_, totalTasks_);
        }
    } else {
        LOG_ERROR(errorMsg);
        emit taskFinished(nodeName, false, duration, errorMsg);
    }

    if (shouldPump) pumpReady();
    checkFinished();
}

// ===========================================================================
// checkFinished
// ===========================================================================
void Engine::checkFinished() {
    bool shouldFinish = false;
    bool success = true;
    QString reason;

    {
        QMutexLocker lock(&planMutex_);
        if (finished_) return;

        if (runningTasks_ == 0) {
            if (aborted_) {
                shouldFinish = true;
                success = false;
                reason = "Build failed";
            } else if (pendingTasks_ == 0) {
                shouldFinish = true;
                success = true;
            } else if (readyQueue_.isEmpty()) {
                shouldFinish = true;
                success = false;
                reason = "Build stalled (possible dependency cycle)";
            }
        }
        if (shouldFinish) finished_ = true;
    }

    if (shouldFinish) {
        if (watchdog_) watchdog_->stop();
        cleanupPlan();
        if (!reason.isEmpty()) emit buildError(reason);
        emit buildFinished(success, buildTimer_.elapsed() / 1000.0);
    }
}

// ===========================================================================
// abortBuild
// ===========================================================================
void Engine::abortBuild(const QString& reason) {
    {
        QMutexLocker lock(&planMutex_);
        if (finished_) return;
        aborted_ = true;
    }
    LOG_ERROR(reason);
    checkFinished();
}

// ===========================================================================
// cleanupPlan
// ===========================================================================
void Engine::cleanupPlan() {
    QMutexLocker lock(&planMutex_);
    qDeleteAll(tasks_);
    tasks_.clear();
    nodeToTask_.clear();
    readyQueue_.clear();
    readySet_.clear();
    runningTasks_ = 0;
    pendingTasks_ = 0;
}

} // namespace pmake::core