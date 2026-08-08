#include "core/graph.h"
#include "util/logging.h"
#include "util/filesystem.h"
#include <algorithm>

namespace pmake::core {

Graph::Graph(const State& state, bool dryRun)
    : state_(state), dryRun_(dryRun) {}

void Graph::build() {
    nodes_.clear();
    QSet<QString> definedOutputs;
    for (const auto& build : state_.builds()) {
        const Rule* rule = state_.getRule(build.rule_name);
        if (!rule) {
            LOG_WARN(QString("Rule '%1' not found for build").arg(build.rule_name));
            continue;
        }
        for (const QString& out : build.outputs) {
            if (out.isEmpty()) {
                LOG_WARN("Build output name is empty, skipping");
                continue;
            }
            if (definedOutputs.contains(out)) {
                LOG_WARN(QString("Duplicate output '%1' ignored").arg(out));
                continue;
            }
            definedOutputs.insert(out);
            auto node = QSharedPointer<Node>::create(out, &build, rule, &state_, this, dryRun_);
            nodes_[out] = node;
        }
    }
    buildEdges();
    if (hasCycle()) throw std::runtime_error("Cycle detected in graph");
}

void Graph::buildEdges() {
    for (const auto& build : state_.builds()) {
        if (build.outputs.isEmpty()) continue;
        Node* outNode = node(build.outputs.first());
        if (!outNode) continue;
        for (const QString& in : build.inputs) {
            if (in.isEmpty()) continue;
            Node* inNode = getOrCreateNode(in);
            outNode->addDependency(inNode);
        }
        for (const QString& in : build.implicit) {
            if (in.isEmpty()) continue;
            Node* inNode = getOrCreateNode(in);
            outNode->addImplicitDependency(inNode);
        }
        for (const QString& in : build.order_only) {
            if (in.isEmpty()) continue;
            Node* inNode = getOrCreateNode(in);
            outNode->addOrderOnlyDependency(inNode);
        }
    }
}

Node* Graph::getOrCreateNode(const QString& name) {
    if (name.isEmpty()) return nullptr;
    if (nodes_.size() >= MAX_NODES) {
        LOG_ERROR("Graph exceeded maximum node count (" + QString::number(MAX_NODES) + "). Possible dependency cycle.");
        return nullptr;
    }
    auto it = nodes_.find(name);
    if (it != nodes_.end()) return it.value().data();
    auto node = QSharedPointer<Node>::create(name, nullptr, nullptr, &state_, this, dryRun_);
    nodes_[name] = node;
    return node.data();
}

Node* Graph::node(const QString& name) const {
    if (name.isEmpty()) return nullptr;
    auto it = nodes_.find(name);
    return (it != nodes_.end()) ? it.value().data() : nullptr;
}

QVector<Node*> Graph::leafNodes() const {
    QVector<Node*> leaves;
    QSet<Node*> unique;
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        if (!it.key().isEmpty())
            unique.insert(it.value().data());
    }
    for (Node* n : unique) if (n->isLeaf()) leaves.append(n);
    return leaves;
}

QVector<Node*> Graph::allNodes() const {
    QVector<Node*> result;
    QSet<Node*> unique;
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        if (!it.key().isEmpty())
            unique.insert(it.value().data());
    }
    result.reserve(unique.size());
    for (Node* n : unique) result.append(n);
    return result;
}

bool Graph::dfs(const QString& nodeName, QSet<QString>& visited,
                QSet<QString>& recursionStack, QVector<Node*>& order) const {
    if (nodeName.isEmpty()) return false;
    if (recursionStack.contains(nodeName)) return true;
    if (visited.contains(nodeName)) return false;
    visited.insert(nodeName);
    recursionStack.insert(nodeName);
    Node* node = this->node(nodeName);
    if (node) {
        for (Node* dep : node->dependencies()) {
            if (dfs(dep->name(), visited, recursionStack, order)) return true;
        }
        for (Node* dep : node->implicitDeps()) {
            if (dfs(dep->name(), visited, recursionStack, order)) return true;
        }
        for (Node* dep : node->orderOnlyDeps()) {
            if (dfs(dep->name(), visited, recursionStack, order)) return true;
        }
    }
    recursionStack.remove(nodeName);
    if (node) order.append(node);
    return false;
}

bool Graph::hasCycle() const {
    QSet<QString> visited, recursion;
    QVector<Node*> dummy;
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        if (it.key().isEmpty()) continue;
        if (!visited.contains(it.key())) {
            if (dfs(it.key(), visited, recursion, dummy)) return true;
        }
    }
    return false;
}

QVector<Node*> Graph::topologicalSort(const QString& target) {
    QVector<Node*> order;
    QSet<QString> visited, recursion;
    if (target.isEmpty() || !nodes_.contains(target)) {
        LOG_ERROR(QString("Target '%1' not found in graph").arg(target));
        return {};
    }
    if (dfs(target, visited, recursion, order)) {
        throw std::runtime_error("Cycle detected");
    }
    return order;
}

QVector<Node*> Graph::affectedNodes(const QVector<QString>& changedLeaves) const {
    QVector<Node*> result;
    QSet<Node*> visited;
    for (const QString& leaf : changedLeaves) {
        if (leaf.isEmpty()) continue;
        Node* leafNode = node(leaf);
        if (!leafNode) continue;
        for (Node* dep : leafNode->reverseDeps()) {
            if (!visited.contains(dep)) {
                visited.insert(dep);
                result.append(dep);
            }
        }
    }
    return result;
}

void Graph::addDepsFromLog(const deps::DepsLog& log) {
    const auto& entries = log.entries();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const QString& output = it.key();
        const auto& inputs = it.value().inputs;
        if (!inputs.isEmpty()) {
            addDepsForOutput(output, inputs);
        }
    }
}

void Graph::addDepsForOutput(const QString& output, const QVector<QString>& inputs) {
    Node* outNode = getOrCreateNode(output);
    if (!outNode) return;

    // Проверяем уже существующие неявные зависимости
    const auto& existingImplicit = outNode->implicitDeps();
    QSet<QString> existingNames;
    for (Node* n : existingImplicit) {
        existingNames.insert(n->name());
    }

    for (const QString& in : inputs) {
        if (in.isEmpty()) continue;
        if (existingNames.contains(in)) continue;
        Node* inNode = getOrCreateNode(in);
        if (inNode) {
            outNode->addImplicitDependency(inNode);
            existingNames.insert(in);
        }
    }
}

} // namespace pmake::core