#include "core/node.h"
#include "core/graph.h"
#include "deps/deps_log.h"
#include "util/filesystem.h"
#include "util/hash.h"
#include "util/string_utils.h"

#include <format>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pmake::core {

Node::Node(std::string_view name, const Build* build, const Rule* rule,
           const State* state, Graph* graph, bool dryRun)
    : name_(name), build_(build), rule_(rule), state_(state),
      graph_(graph), dryRun_(dryRun), finished_(false) {}

void Node::addDependency(Node* dep) {
    if (!dep) return;
    for (Node* d : deps_) if (d == dep) return;
    deps_.push_back(dep);
    dep->addReverseDependency(this);
}

void Node::addImplicitDependency(Node* dep) {
    if (!dep) return;
    for (Node* d : implicit_deps_) if (d == dep) return;
    implicit_deps_.push_back(dep);
    dep->addReverseDependency(this);
}

void Node::addOrderOnlyDependency(Node* dep) {
    if (!dep) return;
    for (Node* d : order_only_deps_) if (d == dep) return;
    order_only_deps_.push_back(dep);
    dep->addReverseOrderOnlyDependency(this);
}

void Node::addReverseDependency(Node* dep) {
    if (!dep) return;
    for (Node* d : reverse_deps_) if (d == dep) return;
    reverse_deps_.push_back(dep);
}

void Node::addReverseOrderOnlyDependency(Node* dep) {
    if (!dep) return;
    for (Node* d : reverse_order_only_deps_) if (d == dep) return;
    reverse_order_only_deps_.push_back(dep);
}

std::int64_t Node::getFileMtime(std::string_view path) {
    return util::getFileMtime(path);
}

std::string Node::computeCommandHash() const {
    if (!rule_ || !build_) return {};
    auto specialVars = getSpecialVars();
    std::string cmd = rule_->command;
    if (state_) {
        cmd = state_->expandVarsWithSpecial(cmd, build_->scope.get(), specialVars);
    }
    return util::sha1_hex(cmd);
}

bool Node::isDirty() {
    if (isPhony()) return false;
    if (!build_ || !rule_) return false;

    if (rule_->generator) {
        bool anyMissing = false;
        for (const auto& out : build_->outputs) {
            if (util::getFileMtime(out) == -1) { anyMissing = true; break; }
        }
        if (anyMissing) return true;

        std::int64_t maxOut = -1;
        for (const auto& out : build_->outputs) {
            std::int64_t m = util::getFileMtime(out);
            if (m > maxOut) maxOut = m;
        }
        auto checkGenDep = [&](Node* dep) -> bool {
            if (dep->isLeaf()) {
                std::int64_t m = util::getFileMtime(dep->name());
                // NEW (#7): missing input форсит rebuild generator-правила.
                // Раньше "m > maxOut" при m == -1 давал false, и удалённый
                // input не замечался. Теперь — пересборка.
                if (m == -1) return true;
                return m > maxOut;
            }
            if (dep->wasBuilt()) return true;
            const Build* db = dep->getBuild();
            if (!db) return false;
            std::int64_t dm = -1;
            for (const auto& out : db->outputs) {
                std::int64_t mm = util::getFileMtime(out);
                if (mm > dm) dm = mm;
            }
            return dm > maxOut;
        };
        for (Node* dep : deps_) if (checkGenDep(dep)) return true;
        for (Node* dep : implicit_deps_) if (checkGenDep(dep)) return true;
        cachedOutputMtime_ = maxOut;
        mtimeValid_ = true;
        return false;
    }

    bool anyExists = false;
    std::int64_t maxOut = -1;
    for (const auto& out : build_->outputs) {
        std::int64_t m = util::getFileMtime(out);
        if (m != -1) {
            anyExists = true;
            if (m > maxOut) maxOut = m;
        } else {
            return true;
        }
    }
    if (!anyExists) return true;

    auto checkNode = [&](Node* dep) -> bool {
        if (dep->isLeaf()) {
            std::int64_t m = util::getFileMtime(dep->name());
            // NEW (#7): missing input → форсим rebuild.
            // -1 означает "файла нет" (соглашение util::getFileMtime).
            // Раньше "-1 > maxOut" = false, и pmake молча продолжал
            // с устаревшим output при удалённом input. Теперь — rebuild,
            // и если команда не справится без input — exit 1.
            //
            // Важно: order-only deps сюда не попадают (checkNode вызывается
            // только для deps_ и implicit_deps_), поэтому real_order_only_missing
            // не ломается — missing order-only по-прежнему легален.
            if (m == -1) return true;
            if (m > maxOut) return true;
        } else {
            if (dep->wasBuilt()) return true;
            const Build* db = dep->getBuild();
            if (!db) return false;
            std::int64_t m = -1;
            for (const auto& out : db->outputs) {
                std::int64_t mm = util::getFileMtime(out);
                if (mm > m) m = mm;
            }
            if (m > maxOut) return true;
        }
        return false;
    };

    for (Node* dep : deps_) if (checkNode(dep)) return true;
    for (Node* dep : implicit_deps_) if (checkNode(dep)) return true;

    if (rule_ && !rule_->depfile.empty()) {
        auto depInputs = deps::DepsLog::instance().inputsFor(name_);
        if (depInputs.empty()) {
            std::println(std::cerr, "Depfile lost for '{}' -> force rebuild", name_);
            return true;
        }
        std::int64_t maxDepMtime = -1;
        for (const auto& in : depInputs) {
            std::int64_t m = util::getFileMtime(in);
            if (m > maxDepMtime) maxDepMtime = m;
        }
        if (maxDepMtime > maxOut) return true;
    }

    if (state_ && !state_->isCacheValid()) {
        std::string currentHash = computeCommandHash();
        if (!currentHash.empty()) {
            auto cached = state_->getCommandHash(name_);
            if (cached.has_value() && *cached != currentHash) return true;
        }
    }

    cachedOutputMtime_ = maxOut;
    mtimeValid_ = true;
    return false;
}

std::unordered_map<std::string, std::string> Node::getSpecialVars() const {
    std::unordered_map<std::string, std::string> specialVars;
    if (!build_ || !rule_) return specialVars;

    std::string quotedOutputs;
    for (std::size_t i = 0; i < build_->outputs.size(); ++i) {
        if (i) quotedOutputs += ' ';
        quotedOutputs += util::quoteForShell(build_->outputs[i]);
    }
    specialVars["out"] = std::move(quotedOutputs);

    std::string quotedInputs;
    for (std::size_t i = 0; i < build_->inputs.size(); ++i) {
        if (i) quotedInputs += ' ';
        quotedInputs += util::quoteForShell(build_->inputs[i]);
    }
    specialVars["in"] = std::move(quotedInputs);

    std::string inNewline;
    for (std::size_t i = 0; i < build_->inputs.size(); ++i) {
        if (i) inNewline += '\n';
        inNewline += build_->inputs[i];
    }
    specialVars["in_newline"] = std::move(inNewline);

    return specialVars;
}

std::unordered_map<std::string, std::int64_t> Node::getOutputMtimesBefore() const {
    std::unordered_map<std::string, std::int64_t> before;
    for (const auto& out : build_->outputs) {
        before[out] = util::getFileMtime(out);
    }
    return before;
}

bool Node::hasOutputChanged(const std::unordered_map<std::string, std::int64_t>& before) const {
    for (const auto& out : build_->outputs) {
        util::FileCache::instance().invalidate(out);
        std::int64_t now = util::getFileMtime(out);
        auto it = before.find(out);
        std::int64_t prev = (it != before.end()) ? it->second : -1;
        if (prev != now) return true;
    }
    return false;
}

void Node::refreshOutputMtime() {
    std::int64_t maxOut = -1;
    for (const auto& out : build_->outputs) {
        util::FileCache::instance().invalidate(out);
        std::int64_t m = util::getFileMtime(out);
        if (m > maxOut) maxOut = m;
    }
    cachedOutputMtime_ = maxOut;
    mtimeValid_ = true;
}

} // namespace pmake::core