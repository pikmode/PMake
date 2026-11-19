#include "core/graph.h"
#include "deps/deps_log.h"

#include <cstdint>
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
        for (const auto& output : build.outputs) {
            Node* outNode = node(output);
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
    struct Frame {
        Node* node;
        std::size_t nextDependency;
    };

    Node* root = this->node(nodeName);
    std::string key(nodeName);
    if (recursionStack.contains(key)) return true;
    if (!visited.insert(key).second) return false;
    if (!root) return false;

    recursionStack.insert(key);
    std::vector<Frame> stack{{root, 0}};
    while (!stack.empty()) {
        Frame& frame = stack.back();
        const auto& deps = frame.node->dependencies();
        const auto& implicit = frame.node->implicitDeps();
        const auto& orderOnly = frame.node->orderOnlyDeps();
        const std::size_t dependencyCount = deps.size() + implicit.size() + orderOnly.size();

        if (frame.nextDependency == dependencyCount) {
            recursionStack.erase(frame.node->name());
            order.push_back(frame.node);
            stack.pop_back();
            continue;
        }

        std::size_t index = frame.nextDependency++;
        Node* dep = index < deps.size()
            ? deps[index]
            : index < deps.size() + implicit.size()
                ? implicit[index - deps.size()]
                : orderOnly[index - deps.size() - implicit.size()];
        const std::string& depName = dep->name();
        if (recursionStack.contains(depName)) return true;
        if (!visited.insert(depName).second) continue;

        recursionStack.insert(depName);
        stack.push_back({dep, 0});
    }
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
    return topologicalSort(std::vector<std::string>{std::string(target)});
}

std::vector<Node*> Graph::topologicalSort(const std::vector<std::string>& targets) {
    std::vector<Node*> order;
    std::unordered_set<std::string> visited, recursion;
    for (const auto& target : targets) {
        if (target.empty() || !nodes_.contains(target)) {
            std::println(std::cerr, "Target '{}' not found in graph", target);
            return {};
        }
        if (dfs(target, visited, recursion, order)) {
            std::println(std::cerr, "Cycle detected during topological sort");
            return {};
        }
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

// ============================================================
// CHANGED (Issue #1 RAM): entry.inputs → entry.inputIds + lookup.
//
// DepsEntry::inputs был vector<string>, стал vector<uint32_t>.
// На этом вызове (старт Engine) конвертируем ids → строки, потому что
// addDepsForOutput принимает vector<string>.
//
// Разово на старте: 350K entries × ~50 уникальных headers-ids
// (после дедупа в StringPool) = ~17.5M строк → в graph уйдёт столько
// же (edges в графе — это строки, не ids; их кеш не касается).
//
// Память: 17.5M graph-edges всё равно нужны, это не DepsLog.
// Но DepsLog после restore из кеша занимает теперь 700 MB, а не 5.6 GB.
// ============================================================
void Graph::addDepsFromLog(const deps::DepsLog& log) {
    const auto& entries = log.entries();
    for (const auto& [output, entry] : entries) {
        if (entry.inputIds.empty()) continue;

        // Раскрываем ids → строки через lookup.
        // 50 уникальных headers = 50 строк, не 500 на каждый output.
        std::vector<std::string> inputStrs;
        inputStrs.reserve(entry.inputIds.size());
        for (std::uint32_t id : entry.inputIds) {
            inputStrs.emplace_back(log.lookup(id));
        }

        addDepsForOutput(output, inputStrs);
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