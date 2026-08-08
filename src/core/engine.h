#pragma once

#include <QObject>
#include <QThreadPool>
#include <QAtomicInt>
#include <QVector>
#include <QHash>
#include <QQueue>
#include <QMutex>
#include <QSet>
#include <memory>
#include <QElapsedTimer>
#include "core/state.h"
#include "core/graph.h"

namespace pmake::core {

class Engine : public QObject {
    Q_OBJECT
public:
    Engine(State& state, int maxJobs, bool dryRun = false, QObject* parent = nullptr);
    ~Engine();

    // Синхронный запуск – блокирует до завершения сборки
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
    void taskFinished(const QString& name, bool success, double duration, const QString& errorMsg = QString());

private:
    // Структура пула
    struct PoolCounter {
        int maxJobs;
        int active;
        QQueue<Node*> waiting;
        QSet<Node*> waitingSet;
    };

    State& state_;
    Graph graph_;
    QThreadPool pool_;
    QAtomicInt tasksCompleted_;
    int totalTasks_ = 0;
    bool buildFailed_ = false;
    int maxJobs_;
    bool dryRun_;
    QElapsedTimer buildTimer_;

    QHash<QString, PoolCounter> poolCounters_;
    QMutex poolMutex_;

    struct Pending {
        int regular = 0;
        int orderOnly = 0;
    };
    QHash<Node*, Pending> pending_;
    QHash<Node*, QVector<Node*>> regularDependents_;
    QHash<Node*, QVector<Node*>> orderOnlyDependents_;
    QMutex scheduleMutex_;

    QSet<const Build*> activeBuilds_;
    QSet<const Build*> completedBuilds_;
    QMutex buildMutex_;

    QSet<Node*> dirtySet_;
    QHash<const Build*, QVector<Node*>> buildToNodes_;

    QVector<Node*> collectDirtyNodes(const QVector<Node*>& order);
    void tryStartNode(Node* node, bool fromPool = false);
    void releaseDependencies(Node* node);
    bool isRealBuild(const Build* build) const;
    void onNodeFinished(Node* node, int exitCode, double duration);
    void releasePoolSlot(Node* node);
    void runInternal(const QString& target);
};

} // namespace pmake::core