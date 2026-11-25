#include "core/node.h"
#include "core/graph.h"
#include "executor/runner.h"
#include "deps/dep_parser.h"
#include "deps/deps_log.h"
#include "util/filesystem.h"
#include "util/string_utils.h"

#include <algorithm>
#include <filesystem>
#include <cstdio>
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

std::mutex commandOutputMutex;

void writeCommandOutput(executor::OutputStream stream, std::string_view text) {
    if (text.empty()) return;
    FILE* destination = stream == executor::OutputStream::Stdout ? stdout : stderr;
    std::fwrite(text.data(), 1, text.size(), destination);
    std::fflush(destination);
}

void forwardCommandOutput(const executor::Result& result) {
    if (result.stdoutOutput.empty() && result.stderrOutput.empty()) return;

    std::lock_guard<std::mutex> lock(commandOutputMutex);
    writeCommandOutput(executor::OutputStream::Stdout, result.stdoutOutput);
    writeCommandOutput(executor::OutputStream::Stderr, result.stderrOutput);
}

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

std::string_view msvcIncludePath(std::string_view line) {
    constexpr std::string_view englishPrefix = "Note: including file:";
    constexpr std::string_view russianPrefix = "Примечание: включение файла:";
    std::size_t first = 0;
    while (first < line.size() && (line[first] == ' ' || line[first] == '\t' ||
                                   line[first] == '\r')) {
        ++first;
    }

    std::string_view trimmed = line.substr(first);
    std::size_t prefixLength = 0;
    if (trimmed.starts_with(englishPrefix)) {
        prefixLength = englishPrefix.size();
    } else if (trimmed.starts_with(russianPrefix)) {
        prefixLength = russianPrefix.size();
    }
    if (prefixLength == 0) return {};

    std::string_view path = trimmed.substr(prefixLength);
    while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) {
        path.remove_prefix(1);
    }
    while (!path.empty() && (path.back() == ' ' || path.back() == '\r' ||
                              path.back() == '\n')) {
        path.remove_suffix(1);
    }
    return path;
}

void extractMsvcIncludes(std::string& output, std::vector<std::string>& includes) {
    std::string filtered;
    std::size_t start = 0;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start);
        const std::size_t lineEnd = end == std::string::npos ? output.size() : end;
        std::string_view line(output.data() + start, lineEnd - start);
        std::string_view path = msvcIncludePath(line);
        if (!path.empty()) {
            includes.emplace_back(path);
        } else {
            filtered.append(line);
            if (end != std::string::npos) filtered.push_back('\n');
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    output = std::move(filtered);
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
            std::error_code ec;
            std::filesystem::remove(rspFilePath, ec);
            if (ec) {
                std::println(std::cerr, "Failed to remove response file '{}': {}",
                             rspFilePath, util::systemErrorMessage(ec));
            }
            return 1;
        }
        specialVars["rspfile"] = rspFilePath;
        command = state_->expandVarsWithSpecial(rule_->command, build_->scope.get(), specialVars);
    }
    auto removeResponseFile = [&] {
        if (rspFilePath.empty()) return;
        std::error_code ec;
        std::filesystem::remove(rspFilePath, ec);
        util::FileCache::instance().invalidate(rspFilePath);
        if (ec) {
            std::println(std::cerr, "Failed to remove response file '{}': {}",
                         rspFilePath, util::systemErrorMessage(ec));
        }
    };
    std::string depfilePath;
    if (rule_->hasDepfile()) {
        depfilePath = state_->expandVarsWithSpecial(
            rule_->depfile, build_->scope.get(), specialVars);
    }
    auto cleanupFailedBuild = [&] {
        for (const auto& out : build_->outputs) {
            std::error_code ec;
            std::filesystem::remove(out, ec);
            util::FileCache::instance().invalidate(out);
            if (ec) {
                std::println(std::cerr, "Failed to remove partial output '{}': {}",
                             out, util::systemErrorMessage(ec));
            }
        }
        removeResponseFile();
        if (rule_->hasDepfile()) {
            std::error_code ec;
            std::filesystem::remove(depfilePath, ec);
            util::FileCache::instance().invalidate(depfilePath);
            if (ec) {
                std::println(std::cerr, "Failed to remove depfile '{}': {}",
                             depfilePath, util::systemErrorMessage(ec));
            }
        }
    };

    if (rule_->hasDepfile()) {
        std::error_code ec;
        std::filesystem::remove(depfilePath, ec);
        util::FileCache::instance().invalidate(depfilePath);
        if (ec) {
            std::println(std::cerr, "Failed to remove stale depfile '{}': {}",
                         depfilePath, util::systemErrorMessage(ec));
            removeResponseFile();
            return 1;
        }
    }

    auto beforeMtimes = getOutputMtimesBefore();

    std::string workDir = getWorkingDirectory(build_, state_);

    executor::Command cmd;
    cmd.workingDirectory = std::move(workDir);
    cmd.captureOutput = true;
    cmd.runInShell = rule_->runInShell;
    const bool usesMsvcDeps = rule_->deps == "msvc";
    std::string pendingStdout;
    std::string pendingStderr;
    bool outputWasStreamed = false;
    cmd.onOutput = [&](executor::OutputStream stream, std::string_view text) {
        std::lock_guard<std::mutex> lock(commandOutputMutex);
        outputWasStreamed = true;
        if (!usesMsvcDeps) {
            writeCommandOutput(stream, text);
            return;
        }

        std::string& pending = stream == executor::OutputStream::Stdout
            ? pendingStdout : pendingStderr;
        pending.append(text);
        std::size_t start = 0;
        while (true) {
            const std::size_t end = pending.find('\n', start);
            if (end == std::string::npos) break;
            const std::string_view line(pending.data() + start, end - start);
            if (msvcIncludePath(line).empty()) {
                writeCommandOutput(stream,
                    std::string_view(pending.data() + start, end - start + 1));
            }
            start = end + 1;
        }
        if (start != 0) pending.erase(0, start);
    };
    if (cmd.runInShell) {
        cmd.shellCommand = std::move(command);
    } else {
        auto args = split_command(command);
        if (args.empty()) {
            std::println(std::cerr, "Failed to split command for '{}'", name_);
            removeResponseFile();
            return 1;
        }
        cmd.program = std::move(args.front());
        args.erase(args.begin());
        cmd.arguments = std::move(args);
    }

    executor::Result res = executor::Runner::runSync(cmd);
    {
        std::lock_guard<std::mutex> lock(commandOutputMutex);
        if (usesMsvcDeps) {
            if (!pendingStdout.empty() &&
                msvcIncludePath(pendingStdout).empty()) {
                writeCommandOutput(executor::OutputStream::Stdout, pendingStdout);
            }
            if (!pendingStderr.empty() &&
                msvcIncludePath(pendingStderr).empty()) {
                writeCommandOutput(executor::OutputStream::Stderr, pendingStderr);
            }
            pendingStdout.clear();
            pendingStderr.clear();
        }
    }
    if (!outputWasStreamed) forwardCommandOutput(res);

    std::vector<std::string> msvcIncludes;
    if (usesMsvcDeps) {
        extractMsvcIncludes(res.stdoutOutput, msvcIncludes);
        extractMsvcIncludes(res.stderrOutput, msvcIncludes);
        std::sort(msvcIncludes.begin(), msvcIncludes.end());
        msvcIncludes.erase(std::unique(msvcIncludes.begin(), msvcIncludes.end()),
                           msvcIncludes.end());
    }

    if (res.exitCode != 0) {
        cleanupFailedBuild();
        return res.exitCode == 2 ? 1 : res.exitCode;
    }

    removeResponseFile();

    if (!rule_->isNinjaTool()) {
        for (const auto& out : build_->outputs) {
            if (!util::fileExists(out)) {
                std::println(std::cerr, "Output file not created: {}", out);
                cleanupFailedBuild();
                return 1;
            }
        }
    }

    if (rule_->hasDepfile()) {
        auto inputs = deps::DepParser::parseChecked(depfilePath);
        if (!inputs) {
            std::println(std::cerr,
                         "Declared depfile '{}' was not generated or is invalid",
                         depfilePath);
            cleanupFailedBuild();
            return 1;
        }
        for (const auto& out : build_->outputs) {
            auto existing = deps::DepsLog::instance().inputsFor(out);
            if (!depsListsEqual(existing, *inputs) ||
                !deps::DepsLog::instance().inputIdsFor(out)) {
                deps::DepsLog::instance().record(out, *inputs);
            }
        }
    } else if (usesMsvcDeps) {
        for (const auto& out : build_->outputs) {
            auto existing = deps::DepsLog::instance().inputsFor(out);
            if (!depsListsEqual(existing, msvcIncludes) ||
                !deps::DepsLog::instance().inputIdsFor(out)) {
                deps::DepsLog::instance().record(out, msvcIncludes);
            }
        }
    }

    refreshOutputMtime();

    if (state_) {
        const std::string hash = computeCommandHash();
        if (!hash.empty()) {
            for (const auto& out : build_->outputs) {
                state_->setCommandHash(out, hash);
            }
        }
    }

    bool outputChanged = hasOutputChanged(beforeMtimes);
    guard.success = true;
    if (rule_->restat && !outputChanged) {
        return 2;
    }

    return 0;
}

} // namespace pmake::core