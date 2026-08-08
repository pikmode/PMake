#pragma once

#include <QString>
#include <QHash>
#include <QVector>
#include <QSet>
#include <QSharedPointer>
#include "core/state.h"
#include "core/node.h"
#include "deps/deps_log.h"

namespace pmake::core {

class Graph {
public:
    static constexpr int MAX_NODES = 100000;

    explicit Graph(const State& state, bool dryRun = false);
    void build();
    Node* getOrCreateNode(const QString& name);
    Node* node(const QString& name) const;
    const QHash<QString, QSharedPointer<Node>>& nodes() const { return nodes_; }
    QVector<Node*> leafNodes() const;
    QVector<Node*> allNodes() const;
    QVector<Node*> topologicalSort(const QString& target);
    bool hasCycle() const;
    QVector<Node*> affectedNodes(const QVector<QString>& changedLeaves) const;

    void addDepsFromLog(const deps::DepsLog& log);
    void addDepsForOutput(const QString& output, const QVector<QString>& inputs);

private:
    const State& state_;
    bool dryRun_;
    QHash<QString, QSharedPointer<Node>> nodes_;
    void buildEdges();
    bool dfs(const QString& nodeName, QSet<QString>& visited,
             QSet<QString>& recursionStack, QVector<Node*>& order) const;
};

} // namespace pmake::core