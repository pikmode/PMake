#include "core/graph.h"
#include "deps/deps_log.h"

#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pmake::core {

Graph::Graph(const State& state, bool dryRun)
    : state_(state), dryRun_(dryRun) {}

bool Graph::build() {
    nodes_.clear();
    std::unordered_set<std::string> definedOutputs;
    bool ok = true;

    for (const auto& build : state_.builds()) {
        const Rule* rule = state_.getRule(build.rule_name);
        if (!rule) {
            std::println(std::cerr, "Unknown rule '{}' for output '{}'",
                         build.rule_name,
                         build.outputs.empty() ? "<none>" : build.outputs.front());
            ok = false;
            continue;
        }
        for (const auto& out : build.outputs) {
            if (out.empty()) {
                std::println(std::cerr, "Build output name is empty");
                ok = false;
                continue;
            }
            if (definedOutputs.contains(out)) {
                std::println(std::cerr, "Duplicate output '{}'", out);
                ok = false;
                continue;
            }
            definedOutputs.insert(out);
            auto node = std::make_shared<Node>(out, &build, rule, &state_, this, dryRun_);
            nodes_[out] = node;
        }
    }

    buildEdges();

    if (hasCycle()) {
        std::println(std::cerr, "Cycle detected in dependency graph");
        ok = false;
    }

    return ok;
}

void Graph::buildEdges() {
    for (const auto& build : state_.builds()) {
        if (build.outputs.empty()) continue;
        Node* outNode = node(build.outputs.front());
        if (!outNode) continue;
        for (const auto& in : build.inputs) {
            if (in.empty()) continue;
            Node* inNode = getOrCreateNode(in);
            outNode->addDependency(inNode);
        }
        for (const auto& in : build.implicit) {
            if (in.empty()) continue;
            Node* inNode = getOrCreateNode(in);
            outNode->addImplicitDependency(inNode);
        }
        for (const auto& in : build.order_only) {
            if (in.empty()) continue;
            Node* inNode = getOrCreateNode(in);
            outNode->addOrderOnlyDependency(inNode);
        }
    }
}

Node* Graph::getOrCreateNode(const std::string& name) {
    if (name.empty()) return nullptr;
    if (nodes_.size() >= static_cast<std::size_t>(MAX_NODES)) {
        std::println(std::cerr,
                     "Graph exceeded maximum node count ({}). Possible dependency cycle.",
                     MAX_NODES);
        return nullptr;
    }
    auto it = nodes_.find(name);
    if (it != nodes_.end()) return it->second.get();
    auto node = std::make_shared<Node>(name, nullptr, nullptr, &state_, this, dryRun_);
    nodes_[name] = node;
    return node.get();
}

Node* Graph::node(std::string_view name) const {
    if (name.empty()) return nullptr;
    auto it = nodes_.find(std::string(name));
    return (it != nodes_.end()) ? it->second.get() : nullptr;
}

std::vector<Node*> Graph::leafNodes() const {
    std::vector<Node*> leaves;
    std::unordered_set<Node*> unique;
    for (const auto& [k, v] : nodes_) {
        if (!k.empty()) unique.insert(v.get());
    }
    for (Node* n : unique) if (n->isLeaf()) leaves.push_back(n);
    return leaves;
}

std::vector<Node*> Graph::allNodes() const {
    std::vector<Node*> result;
    std::unordered_set<Node*> unique;
    for (const auto& [k, v] : nodes_) {
        if (!k.empty()) unique.insert(v.get());
    }
    result.reserve(unique.size());
    for (Node* n : unique) result.push_back(n);
    return result;
}

bool Graph::dfs(std::string_view nodeName,
                std::unordered_set<std::string>& visited,
                std::unordered_set<std::string>& recursionStack,
                std::vector<Node*>& order) const {
    if (nodeName.empty()) return false;
    std::string key(nodeName);
    if (recursionStack.contains(key)) return true;
    if (visited.contains(key)) return false;
    visited.insert(key);
    recursionStack.insert(key);
    Node* n = this->node(nodeName);
    if (n) {
        for (Node* dep : n->dependencies()) {
            if (dfs(dep->name(), visited, recursionStack, order)) return true;
        }
        for (Node* dep : n->implicitDeps()) {
            if (dfs(dep->name(), visited, recursionStack, order)) return true;
        }
        for (Node* dep : n->orderOnlyDeps()) {
            if (dfs(dep->name(), visited, recursionStack, order)) return true;
        }
    }
    recursionStack.erase(key);
    if (n) order.push_back(n);
    return false;
}

bool Graph::hasCycle() const {
    std::unordered_set<std::string> visited, recursion;
    std::vector<Node*> dummy;
    for (const auto& [k, v] : nodes_) {
        if (k.empty()) continue;
        if (!visited.contains(k)) {
            if (dfs(k, visited, recursion, dummy)) return true;
        }
    }
    return false;
}

std::vector<Node*> Graph::topologicalSort(std::string_view target) {
    std::vector<Node*> order;
    std::unordered_set<std::string> visited, recursion;
    if (target.empty() || !nodes_.contains(std::string(target))) {
        std::println(std::cerr, "Target '{}' not found in graph", target);
        return {};
    }
    if (dfs(target, visited, recursion, order)) {
        std::println(std::cerr, "Cycle detected during topological sort");
        return {};
    }
    return order;
}

std::vector<Node*> Graph::affectedNodes(const std::vector<std::string>& changedLeaves) const {
    std::vector<Node*> result;
    std::unordered_set<Node*> visited;
    for (const auto& leaf : changedLeaves) {
        if (leaf.empty()) continue;
        Node* leafNode = node(leaf);
        if (!leafNode) continue;
        for (Node* dep : leafNode->reverseDeps()) {
            if (!visited.contains(dep)) {
                visited.insert(dep);
                result.push_back(dep);
            }
        }
    }
    return result;
}

void Graph::addDepsFromLog(const deps::DepsLog& log) {
    const auto& entries = log.entries();
    for (const auto& [output, entry] : entries) {
        if (!entry.inputs.empty()) {
            addDepsForOutput(output, entry.inputs);
        }
    }
}

void Graph::addDepsForOutput(const std::string& output,
                             const std::vector<std::string>& inputs) {
    Node* outNode = getOrCreateNode(output);
    if (!outNode) return;

    const auto& existingImplicit = outNode->implicitDeps();
    std::unordered_set<std::string> existingNames;
    for (Node* n : existingImplicit) existingNames.insert(n->name());

    for (const auto& in : inputs) {
        if (in.empty()) continue;
        if (existingNames.contains(in)) continue;
        Node* inNode = getOrCreateNode(in);
        if (inNode) {
            outNode->addImplicitDependency(inNode);
            existingNames.insert(in);
        }
    }
}

} // namespace pmake::core