#include "core/node.h"
#include "core/graph.h"
#include "executor/runner.h"
#include "deps/dep_parser.h"
#include "deps/deps_log.h"
#include "util/filesystem.h"
#include "util/string_utils.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pmake::core {

namespace {

std::string getWorkingDirectory(const Build* build, const State* state) {
    if (build && build->scope) {
        auto wd = build->scope->get("workdir");
        if (wd.has_value() && !wd->empty()) {
            return std::filesystem::path(*wd).lexically_normal().string();
        }
    }
    if (state) {
        auto wd = state->getGlobal("workdir");
        if (wd.has_value() && !wd->empty()) {
            return std::filesystem::path(*wd).lexically_normal().string();
        }
    }
    std::error_code ec;
    return std::filesystem::current_path(ec).string();
}

bool depsListsEqual(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.size() != b.size()) return false;
    std::unordered_set<std::string> sa(a.begin(), a.end());
    std::unordered_set<std::string> sb(b.begin(), b.end());
    return sa == sb;
}

// Разбивает строку команды на tokens с учётом кавычек (одинарных и двойных).
// Замена QProcess::splitCommand.
std::vector<std::string> split_command(std::string_view cmd) {
    std::vector<std::string> out;
    std::string cur;
    char quote = 0;
    for (char c : cmd) {
        if (quote) {
            if (c == quote) { quote = 0; }
            else cur.push_back(c);
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == ' ' || c == '\t') {
            if (!cur.empty()) { out.push_back(std::move(cur)); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

} // namespace

int Node::build() {
    if (!build_ || !rule_) return 0;
    if (isPhony()) return 0;

    if (dryRun_) {
        refreshOutputMtime();
        return 0;
    }

    {
        std::lock_guard<std::mutex> lk(buildMutex_);
        if (isBuilding_ || buildSuccess_) return 0;
        isBuilding_ = true;
    }

    struct BuildGuard {
        Node* node;
        bool success = false;
        BuildGuard(Node* n) : node(n) {}
        ~BuildGuard() {
            std::lock_guard<std::mutex> lk(node->buildMutex_);
            node->isBuilding_ = false;
            if (success) node->buildSuccess_ = true;
        }
    } guard(this);

    auto specialVars = getSpecialVars();
    std::string command = rule_->command;
    if (state_) {
        command = state_->expandVarsWithSpecial(command, build_->scope.get(), specialVars);
    }
    if (util::trim(command).empty()) {
        std::println(std::cerr, "Empty command for node '{}'", name_);
        return 1;
    }

    // Создаём директории для outputs
    for (const auto& out : build_->outputs) {
        std::filesystem::path p(out);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
    }

    std::string rspFilePath;
    if (!rule_->rspfile.empty()) {
        rspFilePath = state_->expandVarsWithSpecial(
            rule_->rspfile, build_->scope.get(), specialVars);
        std::string rspContent = state_->expandVarsWithSpecial(
            rule_->rspfile_content, build_->scope.get(), specialVars);
        if (!util::writeFile(rspFilePath, rspContent)) {
            std::println(std::cerr, "Failed to write response file: {}", rspFilePath);
            return 1;
        }
        specialVars["rspfile"] = rspFilePath;
        command = state_->expandVarsWithSpecial(rule_->command, build_->scope.get(), specialVars);
    }

    auto beforeMtimes = getOutputMtimesBefore();

    auto args = split_command(command);
    if (args.empty()) {
        std::println(std::cerr, "Failed to split command for '{}'", name_);
        return 1;
    }
    std::string program = args.front();
    args.erase(args.begin());
    std::string workDir = getWorkingDirectory(build_, state_);

    executor::Command cmd;
    cmd.program = std::move(program);
    cmd.arguments = std::move(args);
    cmd.workingDirectory = std::move(workDir);
    cmd.captureOutput = true;

    executor::Result res = executor::Runner::runSync(cmd);
    if (res.exitCode != 0) {
        std::println(std::cerr, "Build failed for '{}' (exit {})", name_, res.exitCode);
        if (!res.stderrOutput.empty())
            std::println(std::cerr, "STDERR: {}", res.stderrOutput);
        return res.exitCode;
    }

    if (!rspFilePath.empty() && util::fileExists(rspFilePath)) {
        std::error_code ec;
        std::filesystem::remove(rspFilePath, ec);
    }

    for (const auto& out : build_->outputs) {
        if (!util::fileExists(out)) {
            std::println(std::cerr, "Output file not created: {}", out);
            return 1;
        }
    }

    refreshOutputMtime();

    if (state_) {
        const std::string hash = computeCommandHash();
        if (!hash.empty()) {
            state_->setCommandHash(name_, hash);
        }
    }

    guard.success = true;

    bool outputChanged = hasOutputChanged(beforeMtimes);
    if (rule_->restat && !outputChanged) {
        return 2;
    }

    if (rule_ && !rule_->depfile.empty()) {
        std::string depfilePath = rule_->depfile;
        depfilePath = state_->expandVarsWithSpecial(depfilePath, build_->scope.get(), specialVars);
        auto inputs = deps::DepParser::parse(depfilePath);
        if (!inputs.empty()) {
            for (const auto& out : build_->outputs) {
                auto existing = deps::DepsLog::instance().inputsFor(out);
                if (!depsListsEqual(existing, inputs)) {
                    deps::DepsLog::instance().record(out, inputs);
                }
            }
        }
    }

    return 0;
}

} // namespace pmake::core