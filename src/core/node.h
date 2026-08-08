#pragma once

#include <QString>
#include <QVector>
#include <QHash>
#include <QMutex>
#include "core/state.h"

namespace pmake::core {

class Graph;

class Node {
public:
    // Новый параметр dryRun
    Node(const QString& name, const Build* build, const Rule* rule, const State* state, Graph* graph = nullptr, bool dryRun = false);

    const QString& name() const { return name_; }
    const Build* build() const { return build_; }
    const Rule* rule() const { return rule_; }
    const State* state() const { return state_; }
    const Build* getBuild() const { return build_; }

    void addDependency(Node* dep);
    void addImplicitDependency(Node* dep);
    void addOrderOnlyDependency(Node* dep);
    void addReverseDependency(Node* dep);

    const QVector<Node*>& dependencies() const { return deps_; }
    const QVector<Node*>& implicitDeps() const { return implicit_deps_; }
    const QVector<Node*>& orderOnlyDeps() const { return order_only_deps_; }
    const QVector<Node*>& reverseDeps() const { return reverse_deps_; }

    bool isLeaf() const { return build_ == nullptr; }
    bool isPhony() const { return rule_ && rule_->name == "phony"; }

    bool isDirty();
    int build(); // возвращает 0 - успех (изменён), 1 - ошибка, 2 - успех без изменений (restat)
    void refreshOutputMtime();
    qint64 outputMtime() const { return cachedOutputMtime_; }

    void setFinished(bool f) { finished_ = f; }
    bool isFinished() const { return finished_; }

    QHash<QString, QString> getSpecialVars() const;

    QHash<QString, qint64> getOutputMtimesBefore() const;

private:
    QString name_;
    const Build* build_;
    const Rule* rule_;
    const State* state_;
    Graph* graph_;
    bool dryRun_;   // <-- новый флаг

    QVector<Node*> deps_;
    QVector<Node*> implicit_deps_;
    QVector<Node*> order_only_deps_;
    QVector<Node*> reverse_deps_;

    qint64 cachedOutputMtime_ = -1;
    bool mtimeValid_ = false;
    bool finished_ = false;
    mutable QMutex buildMutex_;
    bool isBuilding_ = false;
    bool buildSuccess_ = false;

    qint64 getFileMtime(const QString& path);
    bool hasOutputChanged(const QHash<QString, qint64>& before) const;
};

} // namespace pmake::core