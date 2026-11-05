#include "core/engine.h"
#include "core/node.h"
#include "util/filesystem.h"
#include "util/string_utils.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pmake::core {

std::vector<Node*> Engine::collectDirtyNodes(const std::vector<Node*>& order) {
    std::unordered_set<Node*> reachable;
    for (Node* n : order) reachable.insert(n);

    std::unordered_set<Node*> dirtySet;

    for (Node* node : order) {
        if (node->isLeaf()) {
            std::int64_t currentMtime = util::getFileMtime(node->name());
            auto cached = state_.getFileState(node->name());
            if (!cached.has_value() || cached->mtime != currentMtime) {
                for (Node* consumer : node->reverseDeps()) {
                    if (reachable.contains(consumer)) dirtySet.insert(consumer);
                }
            }
            continue;
        }
        if (node->isDirty()) dirtySet.insert(node);
    }

    std::unordered_set<Node*> expanded = dirtySet;
    std::vector<Node*> queue;
    for (Node* n : dirtySet) if (reachable.contains(n)) queue.push_back(n);

    while (!queue.empty()) {
        Node* n = queue.back();
        queue.pop_back();
        for (Node* consumer : n->reverseDeps()) {
            if (reachable.contains(consumer) && !expanded.contains(consumer)) {
                expanded.insert(consumer);
                queue.push_back(consumer);
            }
        }
    }

    std::vector<Node*> result;
    for (Node* n : expanded) {
        if (n->getBuild() != nullptr) result.push_back(n);
    }
    return result;
}

bool Engine::isRealBuild(const Build* build) const {
    if (!build) return false;
    const Rule* rule = state_.getRule(build->rule_name);
    if (!rule) return false;
    if (rule->name == "phony") return false;
    if (util::trim(rule->command).empty()) return false;
    return true;
}

} // namespace pmake::core