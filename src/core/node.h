#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/state.h"

namespace pmake::core {

class Graph;

class Node {
public:
    Node(std::string_view name, const Build* build, const Rule* rule,
         const State* state, Graph* graph = nullptr, bool dryRun = false);

    const std::string& name() const { return name_; }
    const Build* build() const { return build_; }
    const Rule* rule() const { return rule_; }
    const State* state() const { return state_; }
    const Build* getBuild() const { return build_; }

    void addDependency(Node* dep);
    void addImplicitDependency(Node* dep);
    void addOrderOnlyDependency(Node* dep);
    void addReverseDependency(Node* dep);
    void addReverseOrderOnlyDependency(Node* dep);

    const std::vector<Node*>& dependencies() const { return deps_; }
    const std::vector<Node*>& implicitDeps() const { return implicit_deps_; }
    const std::vector<Node*>& orderOnlyDeps() const { return order_only_deps_; }
    const std::vector<Node*>& reverseDeps() const { return reverse_deps_; }
    const std::vector<Node*>& reverseOrderOnlyDeps() const { return reverse_order_only_deps_; }

    bool isLeaf() const { return build_ == nullptr; }
    bool isPhony() const { return rule_ && rule_->name == "phony"; }

    bool isDirty();
    int build();
    void refreshOutputMtime();
    std::int64_t outputMtime() const { return cachedOutputMtime_; }

    void setFinished(bool f) { finished_ = f; }
    bool isFinished() const { return finished_; }
    void setWasBuilt(bool b) { wasBuilt_ = b; }
    bool wasBuilt() const { return wasBuilt_; }

    std::unordered_map<std::string, std::string> getSpecialVars() const;
    std::unordered_map<std::string, std::int64_t> getOutputMtimesBefore() const;
    std::string computeCommandHash() const;

private:
    std::string name_;
    const Build* build_;
    const Rule* rule_;
    const State* state_;
    Graph* graph_;
    bool dryRun_;

    std::vector<Node*> deps_;
    std::vector<Node*> implicit_deps_;
    std::vector<Node*> order_only_deps_;
    std::vector<Node*> reverse_deps_;
    std::vector<Node*> reverse_order_only_deps_;

    std::int64_t cachedOutputMtime_ = -1;
    bool mtimeValid_ = false;
    bool finished_ = false;
    bool wasBuilt_ = false;
    mutable std::mutex buildMutex_;
    bool isBuilding_ = false;
    bool buildSuccess_ = false;

    std::int64_t getFileMtime(std::string_view path);
    bool hasOutputChanged(const std::unordered_map<std::string, std::int64_t>& before) const;
};

} // namespace pmake::core