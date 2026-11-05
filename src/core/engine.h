#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/graph.h"
#include "core/state.h"

namespace pmake::core {

class Node;

class Engine {
public:
    using BuildStartedFn   = std::function<void()>;
    using BuildProgressFn  = std::function<void(int, int)>;
    using NodeBuiltFn      = std::function<void(const std::string&, int, double)>;
    using BuildFinishedFn  = std::function<void(bool, double)>;
    using BuildErrorFn     = std::function<void(const std::string&)>;
    using TaskStartedFn    = std::function<void(const std::string&, int)>;
    using TaskFinishedFn   = std::function<void(const std::string&, bool, double, const std::string&)>;

    Engine(State& state, int maxJobs, bool dryRun = false);
    ~Engine();

    void onBuildStarted(BuildStartedFn fn)   { buildStarted_ = std::move(fn); }
    void onBuildProgress(BuildProgressFn fn) { buildProgress_ = std::move(fn); }
    void onNodeBuilt(NodeBuiltFn fn)         { nodeBuilt_ = std::move(fn); }
    void onBuildFinished(BuildFinishedFn fn) { buildFinished_ = std::move(fn); }
    void onBuildError(BuildErrorFn fn)       { buildError_ = std::move(fn); }
    void onTaskStarted(TaskStartedFn fn)     { taskStarted_ = std::move(fn); }
    void onTaskFinished(TaskFinishedFn fn)   { taskFinished_ = std::move(fn); }

    bool run(std::string_view target = {});

    void updateFileStates();
    void initializeFileStates();

    bool wasNoop() const { return totalTasks_ == 0; }

private:
    enum class TaskState { Want, Ready, Running, Done, Failed };

    struct SchedTask {
        const Build* build = nullptr;
        std::vector<Node*> nodes;
        Node* runnerNode = nullptr;
        TaskState state = TaskState::Want;
        int pendingDeps = 0;
        std::vector<SchedTask*> dependents;
        std::string poolName = "default";
        bool isReal = false;
    };

    struct PoolCounter {
        int maxJobs = 1;
        int active = 0;
    };

    struct Completion {
        Node* node;
        int exitCode;
        double duration;
    };

    State& state_;
    Graph graph_;
    int maxJobs_;
    bool dryRun_;

    std::chrono::steady_clock::time_point buildStart_;

    mutable std::mutex planMutex_;
    std::unordered_map<const Build*, SchedTask*> tasks_;
    std::unordered_map<Node*, SchedTask*> nodeToTask_;
    std::queue<SchedTask*> readyQueue_;
    std::unordered_set<SchedTask*> readySet_;
    std::unordered_map<std::string, PoolCounter> poolCounters_;

    std::mutex completionMutex_;
    std::condition_variable completionCv_;
    std::queue<Completion> completed_;

    std::vector<std::thread> workers_;

    int runningTasks_ = 0;
    int pendingTasks_ = 0;
    int totalTasks_ = 0;
    int completedTasks_ = 0;
    bool aborted_ = false;
    bool finished_ = false;
    bool success_ = false;
    bool graphBuildFailed_ = false;

    BuildStartedFn   buildStarted_;
    BuildProgressFn  buildProgress_;
    NodeBuiltFn      nodeBuilt_;
    BuildFinishedFn  buildFinished_;
    BuildErrorFn     buildError_;
    TaskStartedFn    taskStarted_;
    TaskFinishedFn   taskFinished_;

    void runInternal(const std::string& target);
    void buildPlan(const std::vector<Node*>& dirtyNodes);
    void pumpReady();
    void startTask(SchedTask* t);
    void workerRoutine(Node* runner);
    void handleNodeDone(Node* node, int exitCode, double duration);
    void checkFinished();
    void abortBuild(const std::string& reason);
    void cleanupPlan();

    std::vector<Node*> collectDirtyNodes(const std::vector<Node*>& order);
    bool isRealBuild(const Build* build) const;
    std::string poolNameFor(const Build* b) const;
};

} // namespace pmake::core