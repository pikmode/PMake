#pragma once

#include <QObject>
#include <QThreadPool>
#include <QVector>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QMutex>
#include <QElapsedTimer>
#include <QTimer>
#include "core/state.h"
#include "core/graph.h"

namespace pmake::core {

class Node;

/**
 * Ninja-style планировщик.
 *
 * Модель: задача = Build (несколько outputs одного Build = одна задача).
 * Состояния: Want -> Ready -> Running -> Done/Failed.
 * Завершение: runningTasks_ == 0 && pendingTasks_ == 0 (под planMutex_).
 * buildFinished эмитится ровно один раз из checkFinished().
 */
class Engine : public QObject {
    Q_OBJECT
public:
    Engine(State& state, int maxJobs, bool dryRun = false, QObject* parent = nullptr);
    ~Engine();

    // Синхронный запуск — блокирует до завершения сборки
    bool run(const QString& target = QString());

    void updateFileStates();
    void initializeFileStates();

signals:
    void buildStarted();
    void buildProgress(int current, int total);
    void nodeBuilt(const QString& node, int exitCode, double duration);
    void buildFinished(bool success, double durationSeconds);
    void buildError(const QString& error);
    void taskStarted(const QString& name, int total);
    void taskFinished(const QString& name, bool success, double duration,
                      const QString& errorMsg = QString());

    // internal: из воркера -> в main-thread через queued connection
    void nodeDoneInternal(pmake::core::Node* node, int exitCode, double duration);

private slots:
    void handleNodeDone(pmake::core::Node* node, int exitCode, double duration);

private:
    enum class TaskState { Want, Ready, Running, Done, Failed };

    struct SchedTask {
        const Build* build = nullptr;
        QVector<Node*> nodes;              // все output-nodes этой задачи
        Node* runnerNode = nullptr;        // node, который вызывает build()
        TaskState state = TaskState::Want;
        int pendingDeps = 0;               // сколько задач-зависимостей ещё не Done
        QVector<SchedTask*> dependents;    // кто ждёт эту задачу
        QString poolName = "default";
        bool isReal = false;               // false для phony / пустых команд
    };

    struct PoolCounter {
        int maxJobs = 1;
        int active = 0;
    };

    State& state_;
    Graph graph_;
    QThreadPool pool_;
    int maxJobs_;
    bool dryRun_;
    QElapsedTimer buildTimer_;
    QTimer* watchdog_ = nullptr;

    // План — всё под planMutex_
    mutable QMutex planMutex_;
    QHash<const Build*, SchedTask*> tasks_;
    QHash<Node*, SchedTask*> nodeToTask_;
    QQueue<SchedTask*> readyQueue_;
    QSet<SchedTask*> readySet_;
    QHash<QString, PoolCounter> poolCounters_;

    int runningTasks_ = 0;    // Running сейчас
    int pendingTasks_ = 0;    // Want + Ready + Running
    int totalTasks_ = 0;      // для прогресса
    int completedTasks_ = 0;  // Done
    bool aborted_ = false;
    bool finished_ = false;
    bool graphBuildFailed_ = false;   // ← NEW: граф не построился (unknown rule, cycle)

    void runInternal(const QString& target);
    void buildPlan(const QVector<Node*>& dirtyNodes);
    void pumpReady();
    void startTask(SchedTask* t);
    void checkFinished();
    void abortBuild(const QString& reason);
    void cleanupPlan();

    QVector<Node*> collectDirtyNodes(const QVector<Node*>& order);
    bool isRealBuild(const Build* build) const;
    QString poolNameFor(const Build* b) const;
};

} // namespace pmake::core

Q_DECLARE_METATYPE(pmake::core::Node*)