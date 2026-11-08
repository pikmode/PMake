#include "core/engine.h"
#include "core/node.h"
#include "deps/deps_log.h"
#include "util/filesystem.h"

#include <chrono>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pmake::core {

Engine::Engine(State& state, int maxJobs, bool dryRun)
    : state_(state)
    , graph_(state, dryRun)
    , maxJobs_(maxJobs > 0 ? maxJobs : 1)
    , dryRun_(dryRun) {

    { PoolCounter pc; pc.maxJobs = maxJobs_; poolCounters_["default"] = pc; }
    { PoolCounter pc; pc.maxJobs = 1;        poolCounters_["console"] = pc; }
    for (const auto& [name, pool] : state_.pools()) {
        PoolCounter pc;
        pc.maxJobs = pool.depth > 0 ? pool.depth : 1;
        poolCounters_[name] = pc;
    }

    graphBuildFailed_ = !graph_.build();
    graph_.addDepsFromLog(deps::DepsLog::instance());
}

Engine::~Engine() {
    {
        std::lock_guard<std::mutex> lk(planMutex_);
        finished_ = true;
    }
    completionCv_.notify_all();
    for (auto& th : workers_) if (th.joinable()) th.join();
    cleanupPlan();
}

bool Engine::run(std::string_view target) {
    if (graphBuildFailed_) {
        if (buildError_) buildError_("Graph build failed — see errors above");
        if (buildFinished_) buildFinished_(false, 0.0);
        return false;
    }

    runInternal(std::string(target));

    while (true) {
        std::unique_lock<std::mutex> lk(completionMutex_);
        completionCv_.wait(lk, [&] {
            return !completed_.empty() || finished_;
        });

        if (finished_ && completed_.empty()) break;

        std::queue<Completion> batch;
        std::swap(batch, completed_);
        lk.unlock();

        while (!batch.empty()) {
            auto c = batch.front(); batch.pop();
            handleNodeDone(c.node, c.exitCode, c.duration);
            if (finished_) break;
        }
        if (finished_) break;
    }

    for (auto& th : workers_) if (th.joinable()) th.join();
    workers_.clear();

    return success_;
}

void Engine::runInternal(const std::string& target) {
    if (buildStarted_) buildStarted_();
    initializeFileStates();
    buildStart_ = std::chrono::steady_clock::now();

    completedTasks_ = 0;
    runningTasks_ = 0;
    pendingTasks_ = 0;
    aborted_ = false;
    finished_ = false;
    success_ = true;

    std::string targetName = target;
    if (targetName.empty()) {
        bool hasAll = false;
        for (const auto& build : state_.builds()) {
            if (!build.outputs.empty() && build.outputs[0] == "all") {
                hasAll = true;
                break;
            }
        }
        if (hasAll) targetName = "all";
        else if (!state_.defaultTargets().empty()) targetName = state_.defaultTargets().front();
        else {
            std::vector<std::string> candidates;
            for (const auto& build : state_.builds())
                for (const auto& out : build.outputs) candidates.push_back(out);
            if (!candidates.empty()) targetName = candidates.back();
            else {
                if (buildError_) buildError_("No target specified and no builds");
                finished_ = true;
                if (buildFinished_) buildFinished_(false, 0.0);
                return;
            }
        }
    }

    std::vector<Node*> order;
    try {
        order = graph_.topologicalSort(targetName);
    } catch (const std::exception& e) {
        if (buildError_) buildError_(std::string("Graph error: ") + e.what());
        finished_ = true;
        auto dur = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart_).count();
        if (buildFinished_) buildFinished_(false, dur);
        return;
    }
    if (order.empty()) {
        if (buildError_) buildError_("Target '" + targetName + "' not found");
        finished_ = true;
        auto dur = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart_).count();
        if (buildFinished_) buildFinished_(false, dur);
        return;
    }

    auto dirtyNodes = collectDirtyNodes(order);
    if (dirtyNodes.empty()) {
        // CHANGED: "All targets up to date" печатает ConsoleUI по taskIndex_==0.
        // Здесь ничего не выводим — избегаем дубля с buildFinished.
        finished_ = true;
        auto dur = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart_).count();
        if (buildFinished_) buildFinished_(true, dur);
        return;
    }

    buildPlan(dirtyNodes);
    if (totalTasks_ == 0) {
        // CHANGED: тот же noop-путь, что и выше — ConsoleUI решит по taskIndex_.
        finished_ = true;
        auto dur = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart_).count();
        if (buildFinished_) buildFinished_(true, dur);
        return;
    }

    if (buildProgress_) buildProgress_(0, totalTasks_);
    pumpReady();
}

// ============================================================
// P1: BATCH STAT для leaves.
// ============================================================
void Engine::initializeFileStates() {
    auto leaves = graph_.leafNodes();

    std::vector<std::string> to_stat;
    to_stat.reserve(leaves.size());
    for (Node* leaf : leaves) {
        if (!state_.getFileState(leaf->name()).has_value()) {
            to_stat.push_back(leaf->name());
        }
    }
    if (to_stat.empty()) return;

    auto batch = util::statBatch(to_stat);
    for (const auto& [path, info] : batch) {
        if (!info.exists) continue;
        FileState fs;
        fs.mtime = info.mtime;
        fs.size  = info.size;
        state_.setFileState(path, fs);
        util::FileCache::instance().setMtime(path, info.mtime);
    }
}

// ============================================================
// P1: BATCH STAT для обновления состояний после сборки.
// ============================================================
void Engine::updateFileStates() {
    auto leaves = graph_.leafNodes();
    if (leaves.empty()) return;

    std::vector<std::string> paths;
    paths.reserve(leaves.size());
    for (Node* leaf : leaves) paths.push_back(leaf->name());

    auto batch = util::statBatch(paths);
    for (const auto& [path, info] : batch) {
        FileState fs;
        fs.mtime = info.exists ? info.mtime : -1;
        fs.size  = info.exists ? info.size  : 0;
        state_.setFileState(path, fs);
        util::FileCache::instance().setMtime(path, fs.mtime);
    }
}

std::string Engine::poolNameFor(const Build* b) const {
    if (b && b->scope) {
        auto pv = b->scope->get("pool");
        if (pv.has_value() && !pv->empty()) return *pv;
    }
    const Rule* rule = b ? state_.getRule(b->rule_name) : nullptr;
    if (rule && rule->pool) return "console";
    return "default";
}

void Engine::workerRoutine(Node* runner) {
    auto t0 = std::chrono::steady_clock::now();
    int exitCode = 0;
    try {
        exitCode = runner->build();
    } catch (const std::exception& e) {
        std::println(std::cerr, "Exception in Node::build(): {}", e.what());
        exitCode = 1;
    } catch (...) {
        std::println(std::cerr, "Unknown exception in Node::build()");
        exitCode = 1;
    }
    double dur = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    {
        std::lock_guard<std::mutex> lk(completionMutex_);
        completed_.push({runner, exitCode, dur});
    }
    completionCv_.notify_one();
}

void Engine::abortBuild(const std::string& reason) {
    {
        std::lock_guard<std::mutex> lk(planMutex_);
        if (finished_) return;
        aborted_ = true;
    }
    std::println(std::cerr, "{}", reason);
    checkFinished();
}

void Engine::cleanupPlan() {
    std::lock_guard<std::mutex> lk(planMutex_);
    for (auto& [b, t] : tasks_) delete t;
    tasks_.clear();
    nodeToTask_.clear();
    while (!readyQueue_.empty()) readyQueue_.pop();
    readySet_.clear();
    runningTasks_ = 0;
    pendingTasks_ = 0;
}

} // namespace pmake::core