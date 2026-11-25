
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#include "core/engine.h"
#include "core/graph.h"
#include "core/node.h"
#include "core/state.h"
#include "parser/parser.h"
#include "parser/ninja-parser/parser.h"
#include "util/logging.h"
#include "util/filesystem.h"
#include "ui/console_ui.h"
#include "ui/Logo.h"
#include "core/cache.h"
#include "deps/deps_log.h"
#include "deps/dep_parser.h"
#include "internal/config.h"

using namespace pmake::core;

namespace {

std::atomic<bool> g_interrupted{false};
std::atomic<int>  g_signal_count{0};

void on_signal(int sig) {
    int n = g_signal_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n >= 2) {
        std::_Exit(128 + sig);
    }
    g_interrupted.store(true, std::memory_order_relaxed);
}

void print_version() {
    std::fprintf(stdout, "%s %s\n", PMAKE_NAME, PMAKE_VERSION);
    std::fflush(stdout);
}

void print_startup_logo() {
    std::string_view version = PMAKE_VERSION;
    constexpr std::string_view prefix = "version ";
    if (version.starts_with(prefix)) version.remove_prefix(prefix.size());

    const auto releaseUrl = std::format(
        "https://github.com/pikmode/PMake/releases/tag/v{}", version);
    std::println("{}", std::string(84, '/'));
    for (std::size_t i = 0; i < Logo::PIKMODE.size(); ++i) {
        std::print("{:<30}  ", Logo::PIKMODE[i]);
        if (i == 2) std::print("{} {}", PMAKE_NAME, PMAKE_VERSION);
        if (i == 3) std::print("{}", releaseUrl);
        if (i == 4) std::print("Developer: PikMode");
        std::println("");
    }
    std::println("{}", std::string(84, '\\'));
    std::println("");
}

// NEW: Перезапуск самого себя для подхвата обновленного манифеста
[[noreturn]] void restart_self(int argc, char** argv) {
#ifdef _WIN32
    auto quoteArg = [](std::string_view arg) {
        std::string quoted = "\"";
        std::size_t backslashes = 0;
        for (char c : arg) {
            if (c == '\\') {
                ++backslashes;
            } else if (c == '"') {
                quoted.append(backslashes * 2 + 1, '\\');
                quoted.push_back('"');
                backslashes = 0;
            } else {
                quoted.append(backslashes, '\\');
                backslashes = 0;
                quoted.push_back(c);
            }
        }
        quoted.append(backslashes * 2, '\\');
        quoted.push_back('"');
        return quoted;
    };

    std::string cmdLine;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) cmdLine += " ";
        cmdLine += quoteArg(argv[i]);
    }
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        DWORD error = ERROR_SUCCESS;
        BOOL gotExitCode = FALSE;
        if (waitResult == WAIT_OBJECT_0) {
            gotExitCode = GetExitCodeProcess(pi.hProcess, &exitCode);
            if (!gotExitCode) error = GetLastError();
        } else {
            error = GetLastError();
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (gotExitCode) std::exit(static_cast<int>(exitCode));
        std::println(stderr, "Failed to wait for restarted PMake (Windows error {})",
                     error);
        std::exit(1);
    }
    DWORD error = GetLastError();
    std::println(stderr, "Failed to restart PMake (Windows error {})", error);
    std::exit(1);
#else
    execvp(argv[0], argv);
    std::perror("execvp");
    std::exit(1);
#endif
}

static bool is_protected_source(std::string_view path) {
    if (path.ends_with("CMakeLists.txt") ||
        path.ends_with("Makefile")     ||
        path.ends_with("build.pmake")  ||
        path.ends_with("build.ninja")) {
        return true;
    }

    static constexpr std::string_view exts[] = {
        ".cmake",
        ".pmake", ".ninja",
        ".c", ".cc", ".cpp", ".cxx", ".c++", ".m", ".mm",
        ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp",
        ".py", ".sh", ".bat", ".ps1",
    };
    for (auto ext : exts) {
        if (path.size() >= ext.size() &&
            path.substr(path.size() - ext.size()) == ext) {
            return true;
        }
    }
    return false;
}

int clean_outputs(const State& state, std::string_view target = {}) {
    std::unordered_set<const Build*> selectedBuilds;
    if (!target.empty()) {
        Graph graph(state, true);
        if (!graph.build()) return 1;
        Node* targetNode = graph.node(target);
        if (!targetNode) {
            LOG_ERROR(std::format("clean: target '{}' not found", target));
            return 1;
        }
        if (!targetNode->getBuild()) {
            LOG_ERROR(std::format("clean: target '{}' has no build outputs", target));
            return 1;
        }
        selectedBuilds.insert(targetNode->getBuild());
    }

    int removed = 0;
    int skipped = 0;
    int failures = 0;

    for (const auto& build : state.builds()) {
        if (!target.empty() && !selectedBuilds.contains(&build)) continue;

        const Rule* rule = state.getRule(build.rule_name);
        if (rule && (rule->generator || rule->name == "phony" || rule->command.empty())) {
            continue;
        }

        for (const auto& out : build.outputs) {
            if (is_protected_source(out)) {
                LOG_VERBOSE(std::format("clean: skipping protected '{}'", out));
                ++skipped;
                continue;
            }
            std::error_code ec;
            if (std::filesystem::remove(std::filesystem::path(out), ec)) ++removed;
            else if (ec) {
                LOG_ERROR(std::format("clean: failed to remove '{}': {}",
                                      out, pmake::util::systemErrorMessage(ec)));
                ++failures;
            }
        }
    }

    if (skipped > 0) {
        LOG_INFO(std::format("Cleaned {} files (skipped {} protected source files)",
                             removed, skipped));
    } else {
        LOG_INFO(std::format("Cleaned {} files", removed));
    }
    return failures == 0 ? 0 : 1;
}

void print_tools_help() {
    std::println(stdout,
        "Usage: {} -t TOOL [target]\n"
        "\n"
        "Tools:\n"
        "  clean [target]    Remove build outputs, optionally for a target\n"
        "  targets           List build targets\n"
        "  commands [target] Print commands for a target and its dependencies\n"
        "  help, list        Show available tools",
        PMAKE_NAME);
}

void print_targets(const State& state) {
    std::unordered_set<std::string> seen;
    for (const auto& build : state.builds()) {
        for (const auto& output : build.outputs) {
            if (!output.empty() && seen.insert(output).second) {
                std::println(stdout, "{}", output);
            }
        }
    }
}

void print_build_command(const State& state, const Build& build) {
    if (build.outputs.empty()) return;
    const Rule* rule = state.getRule(build.rule_name);
    if (!rule || rule->command.empty() || rule->name == "phony") return;

    Node node(build.outputs.front(), &build, rule, &state);
    auto specialVars = node.getSpecialVars();
    if (!rule->rspfile.empty()) {
        specialVars["rspfile"] = state.expandVarsWithSpecial(
            rule->rspfile, build.scope.get(), specialVars);
    }
    std::string command = state.expandVarsWithSpecial(
        rule->command, build.scope.get(), specialVars);
    if (!command.empty()) std::println(stdout, "{}", command);
}

int print_commands(const State& state, std::string_view target) {
    if (target.empty()) {
        for (const auto& build : state.builds()) print_build_command(state, build);
        return 0;
    }

    Graph graph(state, true);
    if (!graph.build()) return 1;

    if (!graph.node(target)) {
        LOG_ERROR(std::format("target '{}' not found", target));
        return 1;
    }

    std::unordered_set<const Build*> seen;
    for (Node* node : graph.topologicalSort(target)) {
        const Build* build = node->getBuild();
        if (build && seen.insert(build).second) print_build_command(state, *build);
    }
    return 0;
}

struct Options {
    bool version   = false;
    bool help      = false;
    bool verbose   = false;
    bool quiet     = false;
    bool dryRun    = false;
    bool noColor   = false;
    std::string buildFile;
    int jobs = 0;
    std::string config = "release";
    std::string platform;
    std::string tool;
    std::string toolTarget;
    std::vector<std::string> targets;
};

int idealThreadCount() {
    unsigned hc = std::thread::hardware_concurrency();
    return hc > 0 ? static_cast<int>(hc) : 1;
}

void printHelp() {
    std::fprintf(stdout,
        "%s %s — modern build system\n"
        "Usage: pmake [options] [targets...]\n"
        "       pmake -t TOOL [target]\n"
        "\n"
        "Options:\n"
        "  -V, --version                Display version information\n"
        "  -f, --file <file>            Use <file> as build file\n"
        "  -j, --jobs <N>               Run <N> jobs in parallel\n"
        "  -t, --tools <tool>           Run a utility tool (use 'list' for tools)\n"
        "      --config <name>          Build configuration (debug/release)\n"
        "      --platform <name>        Target platform (windows/linux/darwin)\n"
        "  -v, --verbose                Show detailed command output\n"
        "  -q, --quiet                  Minimal output\n"
        "  -n, --dry-run                Show what would be built\n"
        "      --no-color               Disable colored output\n"
        "      --license                Show license\n"
        "  -h, --help                   Show this help\n"
        "\n",
        PMAKE_NAME, PMAKE_VERSION);
    std::fflush(stdout);
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

int parse_args(int argc, char** argv, Options& opt) {
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        auto require_next = [&](const char* opt_name) -> bool {
            if (i + 1 >= argc) {
                LOG_ERROR(std::format("option '{}' requires an argument", opt_name));
                return false;
            }
            return true;
        };

        if (arg == "-f" || arg == "--file") {
            if (!require_next("-f")) return 0;
            opt.buildFile = argv[++i];
        } else if (arg == "-j" || arg == "--jobs") {
            if (!require_next("-j")) return 0;
            std::string valStr = argv[++i];
            try {
                size_t pos;
                int val = std::stoi(valStr, &pos);
                if (pos != valStr.length()) throw std::invalid_argument("trailing chars");
                if (val <= 0) {
                    LOG_ERROR(std::format("invalid value '{}' for '-j': must be greater than 0", valStr));
                    return 0;
                }
                opt.jobs = val;
            } catch (...) {
                LOG_ERROR(std::format("invalid value '{}' for '-j': expected an integer", valStr));
                return 0;
            }
        } else if (arg == "-t" || arg == "--tools") {
            if (!require_next("-t")) return 0;
            if (!opt.tool.empty()) {
                LOG_ERROR("only one tool may be selected");
                return 0;
            }
            opt.tool = argv[++i];
        } else if (arg == "--config") {
            if (!require_next("--config")) return 0;
            opt.config = argv[++i];
        } else if (arg == "--platform") {
            if (!require_next("--platform")) return 0;
            opt.platform = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            opt.verbose = true;
        } else if (arg == "-q" || arg == "--quiet") {
            opt.quiet = true;
        } else if (arg == "-n" || arg == "--dry-run") {
            opt.dryRun = true;
        } else if (arg == "--no-color") {
            opt.noColor = true;
        } else if (arg == "--license") {
            std::fprintf(stdout, "%s %s — see LICENSE file\n",             PMAKE_NAME, PMAKE_VERSION);
            std::fflush(stdout);
            return -1;
        } else if (!arg.empty() && arg[0] != '-') {
            positional.emplace_back(argv[i]);
        } else {
            LOG_ERROR(std::format("unknown option '{}'. Try '{} --help'", argv[i], PMAKE_NAME));
            return 0;
        }
    }

    if (!positional.empty()) {
        if (!opt.tool.empty()) {
            if (positional.size() > 1) {
                LOG_ERROR("utility tools accept at most one target argument");
                return 0;
            }
            opt.toolTarget = positional.front();
        } else {
            opt.targets = std::move(positional);
        }
    }

    if (opt.platform.empty()) {
#ifdef _WIN32
        opt.platform = "windows";
#elif __linux__
        opt.platform = "linux";
#elif __APPLE__
        opt.platform = "darwin";
#else
        opt.platform = "unknown";
#endif
    }

    if (opt.jobs <= 0) opt.jobs = idealThreadCount();
    return 1;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    DWORD consoleMode = 0;
    HANDLE consoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (consoleOutput != INVALID_HANDLE_VALUE &&
        GetConsoleMode(consoleOutput, &consoleMode) &&
        !SetConsoleOutputCP(CP_UTF8)) {
        std::fprintf(stderr,
                     "pmake: warning: could not enable UTF-8 console output "
                     "(Windows error %lu)\n",
                     static_cast<unsigned long>(GetLastError()));
    }
#endif

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-V") == 0 || std::strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            printHelp();
            return 0;
        }
    }

    Options opt;
    int rc = parse_args(argc, argv, opt);
    if (rc < 0) return 0;
    if (rc == 0) return 1;

    if (opt.tool == "list" || opt.tool == "help") {
        if (!opt.toolTarget.empty()) {
            std::fputs("utility tool help does not accept a target\n", stderr);
            return 1;
        }
        print_tools_help();
        return 0;
    }
    if (!opt.tool.empty() &&
        opt.tool != "clean" && opt.tool != "targets" && opt.tool != "commands") {
        std::fprintf(stderr, "unknown tool '%s'; run '%s -t list'\n",
                     opt.tool.c_str(), PMAKE_NAME);
        return 1;
    }
    if (!opt.tool.empty() && opt.tool != "commands" && opt.tool != "clean" &&
        !opt.toolTarget.empty()) {
        std::fprintf(stderr, "tool '%s' does not accept a target\n", opt.tool.c_str());
        return 1;
    }

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
#ifdef _WIN32
    std::signal(SIGBREAK, on_signal);
#endif

    std::string buildFile = opt.buildFile;
    if (buildFile.empty()) {
        if (pmake::util::fileExists("build.pmake"))      buildFile = "build.pmake";
        else if (pmake::util::fileExists("build.ninja")) buildFile = "build.ninja";
        else {
            std::fputs("No build file found (build.pmake or build.ninja)\n", stderr);
            return 1;
        }
    }

    pmake::logging::Config logCfg;
    logCfg.log_to_console = true;
    logCfg.color_output   = !opt.noColor && std::getenv("NO_COLOR") == nullptr;
    logCfg.show_timestamp = false;

    if (opt.verbose) {
        logCfg.min_level = pmake::logging::Level::DEBUG;
        pmake::logging::setVerbose(true);
    } else if (opt.quiet) {
        logCfg.min_level = pmake::logging::Level::ERR;
    } else {
        logCfg.min_level = pmake::logging::Level::INFO;
    }
    pmake::logging::init(logCfg);

    bool isNinja = ends_with(to_lower(buildFile), ".ninja");
    if (!isNinja && !ends_with(to_lower(buildFile), ".pmake")) {
        std::string head;
        if (pmake::util::readFile(buildFile, head)) {
            if (head.size() > 512) head.resize(512);
            if (head.find("ninja_required_version") != std::string::npos ||
                head.find("cmake_ninja_workdir")     != std::string::npos) {
                isNinja = true;
            }
        }
    }

    LOG_VERBOSE(std::format("Build file type: {}", isNinja ? "ninja" : "pmake"));

    std::unique_ptr<State> state;
    try {
        if (isNinja) {
            pmake::ninja::Parser ninjaParser;
            state = ninjaParser.parse(buildFile);
        } else {
            pmake::parser::Parser pmakeParser;
            state = pmakeParser.parse(buildFile, opt.config, opt.platform);
        }
    } catch (const std::exception& e) {
        LOG_ERROR(std::format("parser exception: {}", e.what()));
        return 1;
    } catch (...) {
        LOG_ERROR("parser unknown exception");
        return 1;
    }
    if (!state) {
        LOG_ERROR("failed to parse build file");
        return 1;
    }

    if (opt.tool == "clean") return clean_outputs(*state, opt.toolTarget);
    if (opt.tool == "targets") {
        print_targets(*state);
        return 0;
    }
    if (opt.tool == "commands") return print_commands(*state, opt.toolTarget);


    // Generator logic (Manifest Regeneration)

    std::vector<std::string> manifestTargets;
    for (const auto& b : state->builds()) {
        const Rule* r = state->getRule(b.rule_name);
        if (r && r->generator) {
            for (const auto& out : b.outputs) {
                if (out == buildFile) {
                    manifestTargets.push_back(out);
                } else {
                    for (const auto& inc : state->includedFiles()) {
                        if (out == inc) {
                            manifestTargets.push_back(out);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!manifestTargets.empty()) {
        if (opt.verbose) {
            std::println(stderr, "Checking if build manifest needs regeneration...");
        }
        Engine genEngine(*state, 1, opt.dryRun);

        genEngine.onTaskStarted([verbose = opt.verbose](
                const std::string& name, int total,
                const std::string& cmd, const std::string& wd) {
            std::println(stdout, "[1/{}] {}", total, name);
            if (verbose) {
                std::println(stdout, "  command: {}", cmd);
                std::println(stdout, "  workdir: {}", wd);
            }
            std::fflush(stdout);
        });

        bool genResult = true;
        bool generatorDidWork = false;
        for (const auto& t : manifestTargets) {
            if (!genEngine.run(t)) {
                genResult = false;
            } else if (!genEngine.wasNoop() && !genEngine.wasRestatNoop()) {
                generatorDidWork = true;
            }
        }

        if (!genResult) return 1;

        if (generatorDidWork && !opt.dryRun) {
            std::println(stderr, "Build manifest was regenerated. Restarting pmake...");
            restart_self(argc, argv);
        }
    }
    // ============================================================

    if (opt.targets.empty()) {
        bool hasAll = false;
        for (const auto& b : state->builds()) {
            if (!b.outputs.empty() && b.outputs[0] == "all") { hasAll = true; break; }
        }
        if (hasAll) opt.targets.emplace_back("all");
        else if (!state->defaultTargets().empty()) opt.targets = state->defaultTargets();
    }

    bool useCache = true;
    auto cacheVar = state->getGlobal("cache");
    if (cacheVar.has_value() && to_lower(*cacheVar) == "false") useCache = false;

    pmake::core::Cache cache;
    bool cacheValid = false;
    const std::string cachePath = "Project-cache.pmake";

    if (useCache) {
        if (cache.load(cachePath)) {
            for (const auto& [path, fs] : cache.fileStates()) state->setFileState(path, fs);
            state->setCommandHashes(cache.commandHashes());

            if (cache.getConfig() != opt.config || cache.getPlatform() != opt.platform) {
                LOG_VERBOSE("cache: config or platform changed, forcing rebuild");
                state->setForceRebuild(true);
                cacheValid = false;
            } else {
                if (cache.isValid(buildFile, state->includedFiles())) {
                    cacheValid = true;
                } else {
                    LOG_VERBOSE("cache: invalid (build manifests changed)");
                    pmake::deps::DepsLog::instance().clear();
                }
            }
        } else {
            LOG_VERBOSE("cache: not found");
        }
    }
    state->setCacheValid(cacheValid);

    if (!cacheValid) {
        LOG_VERBOSE("re-scanning depfiles");
        for (const auto& build : state->builds()) {
            const Rule* rule = state->getRule(build.rule_name);
            if (!rule || rule->name == "phony" || rule->generator || !rule->hasDepfile()) continue;

            for (const auto& out : build.outputs) {
                std::unordered_map<std::string, std::string> specialVars;
                specialVars["out"] = out;
                std::string joined;
                for (std::size_t i = 0; i < build.inputs.size(); ++i) {
                    if (i) joined += ' ';
                    joined += build.inputs[i];
                }
                specialVars["in"] = std::move(joined);

                std::string depfilePath = state->expandVarsWithSpecial(
                    rule->depfile, build.scope.get(), specialVars);
                auto inputs = pmake::deps::DepParser::parse(depfilePath);
                if (!inputs.empty()) pmake::deps::DepsLog::instance().record(out, inputs);
            }
        }
    }

    if (useCache && !opt.dryRun && !cacheValid) {
        cache.store(*state, buildFile, opt.config, opt.platform);
        if (!cache.save(cachePath)) LOG_WARN("failed to save cache");
        else LOG_VERBOSE("cache: saved");
    }

    pmake::core::Engine engine(*state, opt.jobs, opt.dryRun);
    engine.setCancelFlag(&g_interrupted);

    pmake::ui::ConsoleUI ui(opt.quiet, logCfg.color_output, opt.verbose,
                            opt.dryRun);

    std::once_flag startupBanner;
    engine.onTaskStarted([&ui, &startupBanner, quiet = opt.quiet,
                          verbose = opt.verbose](
            const std::string& name, int total,
            const std::string& cmd, const std::string& wd) {
        if (!quiet) {
            std::call_once(startupBanner, [] {
                print_startup_logo();
                std::fflush(stdout);
            });
        }
        ui.onTaskStarted(name, total);
        if (verbose) ui.onTaskVerbose(cmd, wd);
    });
    engine.onTaskFinished([&ui](const std::string& name, bool ok, double dur, const std::string& err) {
        ui.onTaskFinished(name, ok, dur, err);
    });
    engine.onBuildFinished([&ui](bool ok, double dur, bool interrupted) {
        ui.onBuildFinished(ok, dur, interrupted);
    });

    bool result = engine.run(opt.targets);

    if (g_interrupted.load()) {
        return 130;
    }

    if (!opt.dryRun && useCache && !engine.wasNoop()) {
        engine.updateFileStates();
        cache.updateResults(*state);
        if (!cache.save(cachePath)) LOG_WARN("failed to save cache after build");
    }

    return result ? 0 : 1;
}