#include "core/engine.h"
#include "core/node.h"
#include "deps/deps_log.h"
#include "util/filesystem.h"

#include <algorithm>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <print>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pmake::core {

void Engine::buildPlan(const std::vector<Node*>& dirtyNodesIn) {
    std::lock_guard<std::mutex> lk(planMutex_);

    std::vector<Node*> dirtyNodes = dirtyNodesIn;
    std::stable_sort(dirtyNodes.begin(), dirtyNodes.end(),
        [](Node* a, Node* b) {
            const Build* ba = a ? a->getBuild() : nullptr;
            const Build* bb = b ? b->getBuild() : nullptr;
            if (!ba && !bb) return false;
            if (!ba) return false;
            if (!bb) return true;
            if (ba->rule_name != bb->rule_name) return ba->rule_name < bb->rule_name;
            return a->name() < b->name();
        });

    std::unordered_map<const Build*, std::vector<Node*>> buildToNodes;
    std::vector<const Build*> buildOrder;

    for (Node* n : dirtyNodes) {
        if (!n) continue;
        const Build* b = n->getBuild();
        if (!b) continue;
        auto it = buildToNodes.find(b);
        if (it == buildToNodes.end()) {
            buildToNodes[b] = {};
            buildOrder.push_back(b);
            it = buildToNodes.find(b);
        }
        it->second.push_back(n);
    }

    for (const Build* b : buildOrder) {
        const auto& nodes = buildToNodes[b];
        SchedTask* t = new SchedTask();
        t->build = b;
        t->nodes = nodes;
        t->runnerNode = nodes.empty() ? nullptr : nodes.front();
        t->isReal = isRealBuild(b);
        t->poolName = poolNameFor(b);
        if (!poolCounters_.contains(t->poolName)) t->poolName = "default";

        tasks_[b] = t;
        for (Node* n : nodes) nodeToTask_[n] = t;
    }

    for (auto& [b, t] : tasks_) {
        std::unordered_set<SchedTask*> taskDeps;
        auto scan = [&](Node* node) {
            std::unordered_set<Node*> visiting;
            std::function<void(Node*)> visit = [&](Node* dep) {
                if (!dep) return;
                auto tit = nodeToTask_.find(dep);
                if (tit != nodeToTask_.end()) {
                    SchedTask* dt = tit->second;
                    if (dt && dt != t) taskDeps.insert(dt);
                    return;
                }
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
        t->pendingDeps = static_cast<int>(taskDeps.size());
        for (SchedTask* dt : taskDeps) dt->dependents.push_back(t);
    }

    totalTasks_ = 0;
    for (auto& [b, t] : tasks_) {
        ++pendingTasks_;
        if (t->isReal) ++totalTasks_;
    }

    for (const Build* b : buildOrder) {
        auto it = tasks_.find(b);
        if (it == tasks_.end()) continue;
        SchedTask* t = it->second;
        if (t->pendingDeps == 0) {
            t->state = TaskState::Ready;
            readyQueue_.push(t);
            readySet_.insert(t);
        }
    }
}

void Engine::pumpReady() {
    if (finished_) return;

    std::vector<SchedTask*> toStart;
    {
        std::lock_guard<std::mutex> lk(planMutex_);
        if (finished_ || aborted_) return;

        while (!readyQueue_.empty()) {
            SchedTask* t = readyQueue_.front();
            if (!t) { readyQueue_.pop(); continue; }

            PoolCounter& pc = poolCounters_[t->poolName];
            if (pc.active >= pc.maxJobs) break;

            readyQueue_.pop();
            readySet_.erase(t);

            t->state = TaskState::Running;
            pc.active++;
            runningTasks_++;
            toStart.push_back(t);
        }
    }

    for (SchedTask* t : toStart) startTask(t);
    checkFinished();
}

void Engine::startTask(SchedTask* t) {
    if (!t || !t->runnerNode) return;

    if (!t->isReal) {
        // phony — сразу Done через completion queue
        {
            std::lock_guard<std::mutex> lk(completionMutex_);
            completed_.push({t->runnerNode, 0, 0.0});
        }
        completionCv_.notify_one();
        return;
    }

    if (!dryRun_ && !t->runnerNode->isDirty()) {
        {
            std::lock_guard<std::mutex> lk(completionMutex_);
            completed_.push({t->runnerNode, 2, 0.0});
        }
        completionCv_.notify_one();
        return;
    }

    std::string display = (t->build && !t->build->outputs.empty())
                          ? t->build->outputs.front()
                          : t->runnerNode->name();

    const Rule* rule = t->runnerNode->rule();
    if (rule && !rule->description.empty()) {
        auto specialVars = t->runnerNode->getSpecialVars();
        display = state_.expandVarsWithSpecial(
            rule->description,
            t->build ? t->build->scope.get() : nullptr,
            specialVars);
    }
    if (taskStarted_) taskStarted_(display, totalTasks_);

    Node* runner = t->runnerNode;
    workers_.emplace_back([this, runner]() { workerRoutine(runner); });
}

void Engine::handleNodeDone(Node* node, int exitCode, double duration) {
    if (!node) return;

    std::string nodeName = node->name();
    bool success = false;
    bool wasReal = false;
    std::string errorMsg;
    bool shouldPump = false;
    std::vector<Node*> affectedNodes;

    {
        std::lock_guard<std::mutex> lk(planMutex_);
        if (finished_) return;

        auto tit = nodeToTask_.find(node);
        if (tit == nodeToTask_.end()) return;
        SchedTask* t = tit->second;
        if (!t || t->state != TaskState::Running) return;

        if (t->build && !t->build->outputs.empty()) {
            nodeName = t->build->outputs.front();
        }

        auto pit = poolCounters_.find(t->poolName);
        if (pit != poolCounters_.end() && pit->second.active > 0) pit->second.active--;

        runningTasks_--;
        pendingTasks_--;

        bool ok = (exitCode == 0 || exitCode == 2);
        success = ok;
        wasReal = t->isReal;

        if (!ok) {
            t->state = TaskState::Failed;
            errorMsg = std::format("Build failed for '{}' (exit {})", nodeName, exitCode);
            aborted_ = true;
        } else {
            t->state = TaskState::Done;
            if (t->isReal) completedTasks_++;
            affectedNodes = t->nodes;

            for (Node* n : t->nodes) {
                n->setFinished(true);
                if (exitCode == 0) n->setWasBuilt(true);
            }

            for (SchedTask* dt : t->dependents) {
                if (!dt || dt->state != TaskState::Want) continue;
                if (dt->pendingDeps > 0) dt->pendingDeps--;
                if (dt->pendingDeps == 0) {
                    dt->state = TaskState::Ready;
                    readyQueue_.push(dt);
                    readySet_.insert(dt);
                }
            }
            shouldPump = true;
        }
    }

    if (success) {
        for (Node* n : affectedNodes) {
            auto inputs = deps::DepsLog::instance().inputsFor(n->name());
            if (!inputs.empty()) graph_.addDepsForOutput(n->name(), inputs);
        }
        if (wasReal) {
            if (nodeBuilt_) nodeBuilt_(nodeName, exitCode, duration);
            if (taskFinished_) taskFinished_(nodeName, true, duration, {});
            if (buildProgress_) buildProgress_(completedTasks_, totalTasks_);
        }
    } else {
        std::println(std::cerr, "{}", errorMsg);
        if (taskFinished_) taskFinished_(nodeName, false, duration, errorMsg);
    }

    if (shouldPump) pumpReady();
    checkFinished();
}

void Engine::checkFinished() {
    bool shouldFinish = false;
    bool success = true;
    std::string reason;

    {
        std::lock_guard<std::mutex> lk(planMutex_);
        if (finished_) return;

        if (runningTasks_ == 0) {
            if (aborted_) {
                shouldFinish = true;
                success = false;
                reason = "Build failed";
            } else if (pendingTasks_ == 0) {
                shouldFinish = true;
                success = true;
            } else if (readyQueue_.empty()) {
                shouldFinish = true;
                success = false;
                reason = "Build stalled (possible dependency cycle)";
            }
        }
        if (shouldFinish) {
            finished_ = true;
            success_ = success;
        }
    }

    if (shouldFinish) {
        cleanupPlan();
        if (!reason.empty() && buildError_) buildError_(reason);
        auto dur = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - buildStart_).count();
        if (buildFinished_) buildFinished_(success, dur);
        completionCv_.notify_all();
    }
}

} // namespace pmake::core