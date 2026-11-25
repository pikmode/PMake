#include "core/engine.h"
#include "core/node.h"
#include "deps/deps_log.h"
#include "util/filesystem.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cstdint>
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
    // P2 (Issue #1): PREFETCH — собрать ВСЕ mtime, которые могут
    // понадобиться isDirty():
    //   * leaves (sources)
    //   * outputs всех build-нод
    //   * depfile-inputs (~500 headers/build)
    //
    // CHANGED (RAM fix): depfile-inputs теперь как uint32 ids, а не
    // строки. Дедуп через unordered_set<uint32_t> — на Chromium это
    // 50K уникальных ids из 175M вставок.
    // ============================================================
    std::vector<std::string> paths_to_stat;
    paths_to_stat.reserve(order.size() * 2);

    std::unordered_set<std::uint32_t> dep_ids_seen;
    dep_ids_seen.reserve(order.size());

    auto& depsLog = deps::DepsLog::instance();

    for (Node* node : order) {
        if (node->isLeaf()) {
            paths_to_stat.push_back(node->name());
        } else if (const Build* b = node->getBuild()) {
            for (const auto& out : b->outputs) {
                paths_to_stat.push_back(out);
            }
        }
        const Rule* r = node->rule();
        if (r && r->tracksDependencies()) {
            const auto* idlist = depsLog.inputIdsFor(node->name());
            if (idlist) {
                for (std::uint32_t id : *idlist) {
                    dep_ids_seen.insert(id);
                }
            }
        }
    }

    // Конвертируем уникальные ids → строки
    paths_to_stat.reserve(paths_to_stat.size() + dep_ids_seen.size());
    for (std::uint32_t id : dep_ids_seen) {
        paths_to_stat.emplace_back(depsLog.lookup(id));
    }

    // Финальный дедуп (leaves/outputs могут пересекаться с headers)
    if (!paths_to_stat.empty()) {
        std::sort(paths_to_stat.begin(), paths_to_stat.end());
        paths_to_stat.erase(
            std::unique(paths_to_stat.begin(), paths_to_stat.end()),
            paths_to_stat.end());
    }

    if (!paths_to_stat.empty()) {
        auto batch = util::statBatch(paths_to_stat);
        for (const auto& [path, info] : batch) {
            util::FileCache::instance().setMtime(
                path, info.exists ? info.mtime : -1);
        }
    }

    // ============================================================
    // P3 (Issue #1): MEMOIZE isDirty.
    // ============================================================
    std::unordered_map<Node*, bool> isDirtyCache;
    isDirtyCache.reserve(order.size());

    auto isDirtyMemo = [&](Node* n) -> bool {
        auto [it, inserted] = isDirtyCache.try_emplace(n, false);
        if (inserted) it->second = n->isDirty();
        return it->second;
    };

    std::unordered_set<Node*> dirtySet;

    for (Node* node : order) {
        if (node->isLeaf()) {
            std::int64_t currentMtime = util::getFileMtime(node->name());
            auto cached = state_.getFileState(node->name());
            if (!cached.has_value() || cached->mtime != currentMtime) {
                for (Node* consumer : node->reverseDeps()) {
                    if (reachable.contains(consumer) && isDirtyMemo(consumer)) {
                        dirtySet.insert(consumer);
                    }
                }
            }
            continue;
        }
        if (isDirtyMemo(node)) {
            dirtySet.insert(node);
        }
    }

    // BFS: расширяем на всех downstream
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