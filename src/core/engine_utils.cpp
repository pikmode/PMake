#include "core/engine.h"
#include "util/logging.h"
#include "util/filesystem.h"
#include <QFileInfo>

namespace pmake::core {

QVector<Node*> Engine::collectDirtyNodes(const QVector<Node*>& order) {
    QSet<Node*> reachable;
    for (Node* n : order) reachable.insert(n);

    QSet<Node*> dirtySet;

    for (Node* node : order) {
        if (node->isLeaf()) {
            qint64 currentMtime = util::getFileMtime(node->name());
            auto cached = state_.getFileState(node->name());
            if (!cached.has_value() || cached->mtime != currentMtime) {
                for (Node* consumer : node->reverseDeps()) {
                    if (reachable.contains(consumer)) {
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

    // BFS: распространяем dirty на ВСЕХ downstream-потребителей,
    // а не только на 1 уровень. Иначе touch a.in -> a.out -> b.out,
    // а c.out (2-й уровень) не пересобирается.
    QSet<Node*> expanded = dirtySet;
    QVector<Node*> queue;
    for (Node* n : dirtySet) {
        if (reachable.contains(n)) queue.append(n);
    }

    while (!queue.isEmpty()) {
        Node* n = queue.takeLast();
        for (Node* consumer : n->reverseDeps()) {
            if (reachable.contains(consumer) && !expanded.contains(consumer)) {
                expanded.insert(consumer);
                queue.append(consumer);
            }
        }
    }

    QVector<Node*> result;
    for (Node* n : expanded) {
        if (n->getBuild() != nullptr) {
            result.append(n);
        }
    }
    return result;
}

bool Engine::isRealBuild(const Build* build) const {
    if (!build) return false;
    const Rule* rule = state_.getRule(build->rule_name);
    if (!rule) return false;
    if (rule->name == "phony") return false;
    if (rule->command.trimmed().isEmpty()) return false;
    return true;
}

} // namespace pmake::core