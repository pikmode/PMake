#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/state.h"
#include "core/node.h"

namespace pmake::deps { class DepsLog; }// forward (определён в deps/deps_log.h)

namespace pmake::core {

class Graph {
public:
    static constexpr int MAX_NODES = 100000;

    explicit Graph(const State& state, bool dryRun = false);
    bool build();

    Node* getOrCreateNode(const std::string& name);
    Node* node(std::string_view name) const;
    const std::unordered_map<std::string, std::shared_ptr<Node>>& nodes() const { return nodes_; }

    std::vector<Node*> leafNodes() const;
    std::vector<Node*> allNodes() const;
    std::vector<Node*> topologicalSort(std::string_view target);
    bool hasCycle() const;
    std::vector<Node*> affectedNodes(const std::vector<std::string>& changedLeaves) const;

    void addDepsFromLog(const pmake::deps::DepsLog& log);
    void addDepsForOutput(const std::string& output, const std::vector<std::string>& inputs);

private:
    const State& state_;
    bool dryRun_;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes_;

    void buildEdges();
    bool dfs(std::string_view nodeName,
             std::unordered_set<std::string>& visited,
             std::unordered_set<std::string>& recursionStack,
             std::vector<Node*>& order) const;
};

} // namespace pmake::core