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

    // ============================================================
    // P1: BATCH STAT — прогреваем FileCache ОДНИМ вызовом.
    //
    // Собираем ВСЕ пути, которые могут понадобиться isDirty():
    //   * leaves (input-файлы) — их mtime сравнивается с output
    //   * outputs всех build-нод — их mtime тоже проверяется
    //
    // После statBatch: любая getFileMtime() → попадание в FileCache,
    // значит 0 syscalls на N файлов вместо N syscalls.
    // ============================================================
    std::vector<std::string> paths_to_stat;
    paths_to_stat.reserve(order.size() * 2);

    for (Node* node : order) {
        if (node->isLeaf()) {
            paths_to_stat.push_back(node->name());
        }
        const Build* b = node->getBuild();
        if (b) {
            for (const auto& out : b->outputs) {
                paths_to_stat.push_back(out);
            }
        }
    }

    if (!paths_to_stat.empty()) {
        auto batch = util::statBatch(paths_to_stat);
        for (const auto& [path, info] : batch) {
            util::FileCache::instance().setMtime(
                path, info.exists ? info.mtime : -1);
        }
    }

    // ============================================================
    // Теперь обычная логика — getFileMtime читает из прогретого кэша.
    // ============================================================
    std::unordered_set<Node*> dirtySet;

    for (Node* node : order) {
        if (node->isLeaf()) {
            std::int64_t currentMtime = util::getFileMtime(node->name());
            auto cached = state_.getFileState(node->name());
            if (!cached.has_value() || cached->mtime != currentMtime) {
                for (Node* consumer : node->reverseDeps()) {
                    // Фильтр isDirty(): не включаем consumer'ов, которые
                    // реально up-to-date (иначе [1/38] на одной задаче).
                    if (reachable.contains(consumer) && consumer->isDirty()) {
                        dirtySet.insert(consumer);
                    }
                }
            }
            continue;
        }
        if (node->isDirty()) {
            dirtySet.insert(node);
        }
    }

    // BFS: расширяем на всех downstream (без фильтра isDirty — их input
    // ещё не изменился, потому что A ещё не выполнился).
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