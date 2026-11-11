#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#include "core/engine.h"
#include "core/state.h"
#include "parser/parser.h"
#include "parser/ninja-parser/parser.h"
#include "util/logging.h"
#include "util/filesystem.h"
#include "ui/console_ui.h"
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

// NEW: Перезапуск самого себя для подхвата обновленного манифеста
[[noreturn]] void restart_self(int argc, char** argv) {
#ifdef _WIN32
    std::string cmdLine;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) cmdLine += " ";
        cmdLine += argv[i];
    }
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::exit(static_cast<int>(exitCode));
    }
    std::exit(1);
#else
    // CHANGED: execvp ищет бинарник в PATH, execv требует полный путь.
    // Если пользователь запустил 'pmake' из PATH, execv упадет.
    execvp(argv[0], argv);
    std::perror("execvp");
    std::exit(1);
#endif
}

struct Options {
    bool version   = false;
    bool help      = false;
    bool verbose   = false;
    bool quiet     = false;
    bool dryRun    = false;
    bool noColor   = false;
    bool clean     = false;
    std::string buildFile;
    int jobs = 0;
    std::string config = "release";
    std::string platform;
    std::string command = "build";
    std::string target;
};

int idealThreadCount() {
    unsigned hc = std::thread::hardware_concurrency();
    return hc > 0 ? static_cast<int>(hc) : 1;
}

void printHelp() {
    std::fprintf(stdout,
        "%s %s — modern build system\n"
        "Usage: pmake [options] [command] [target]\n"
        "\n"
        "Options:\n"
        "  -V, --version                Display version information\n"
        "  -f, --file <file>            Use <file> as build file\n"
        "  -j, --jobs <N>               Run <N> jobs in parallel\n"
        "      --clean                  Clean all outputs\n"
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
        } else if (arg == "--clean") {
            opt.clean = true;
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
            std::fprintf(stdout, "%s %s — see LICENSE file\n", PMAKE_NAME, PMAKE_VERSION);
            std::fflush(stdout);
            return -1;
        } else if (!arg.empty() && arg[0] != '-') {
            positional.emplace_back(argv[i]);
        } else {
            LOG_ERROR(std::format("unknown option '{}'. Try '{} --help'", argv[i], PMAKE_NAME));
            return 0;
        }
    }

    if (!positional.empty()) opt.command = positional[0];
    if (positional.size() > 1) opt.target = positional[1];

    if (opt.command == "build" && positional.size() > 1) {
        if (positional[1] == "debug" || positional[1] == "release") {
            opt.config = positional[1];
            if (positional.size() > 2) opt.target = positional[2];
        } else {
            opt.target = positional[1];
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

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
#ifdef _WIN32
    std::signal(SIGBREAK, on_signal); // NEW: ловим CTRL_BREAK_EVENT от раннера
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

    // ============================================================
    // NEW: Generator logic (Manifest Regeneration)
    // ============================================================
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
        bool genResult = true;
        for (const auto& t : manifestTargets) {
            if (!genEngine.run(t)) genResult = false;
        }
        
        if (!genResult) return 1;
        
        // Если сборка манифеста реально что-то сделала (не noop) и это не dry-run
        if (!genEngine.wasNoop() && !opt.dryRun) {
            std::println(stderr, "Build manifest was regenerated. Restarting pmake...");
            restart_self(argc, argv);
        }
    }
    // ============================================================

    if (opt.target.empty() && opt.command == "build") {
        bool hasAll = false;
        for (const auto& b : state->builds()) {
            if (!b.outputs.empty() && b.outputs[0] == "all") { hasAll = true; break; }
        }
        if (hasAll) opt.target = "all";
        else if (!state->defaultTargets().empty()) opt.target = state->defaultTargets().front();
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
                std::int64_t buildMtime = pmake::util::getFileMtime(buildFile);
                std::int64_t cacheMtime = pmake::util::getFileMtime(cachePath);
                bool buildFileNewer = (buildMtime != -1 && cacheMtime != -1 && buildMtime > cacheMtime);

                if (cache.isValid(buildFile) && !buildFileNewer) {
                    cacheValid = true;
                } else {
                    LOG_VERBOSE("cache: invalid (build file changed)");
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
            if (!rule || rule->name == "phony" || rule->generator || rule->depfile.empty()) continue;

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

    if (opt.clean || opt.command == "clean") {
        int removed = 0;
        for (const auto& build : state->builds()) {
            const Rule* rule = state->getRule(build.rule_name);
            if (rule && rule->generator) continue;
            for (const auto& out : build.outputs) {
                std::error_code ec;
                if (std::filesystem::remove(std::filesystem::path(out), ec)) ++removed;
            }
        }
        LOG_INFO(std::format("Cleaned {} files", removed));
        return 0;
    }

    if (opt.command != "build") {
        LOG_ERROR(std::format("unknown command '{}'", opt.command));
        return 1;
    }

    pmake::core::Engine engine(*state, opt.jobs, opt.dryRun);
    engine.setCancelFlag(&g_interrupted);

    pmake::ui::ConsoleUI ui(opt.quiet, logCfg.color_output, opt.verbose);

    engine.onTaskStarted([&ui, verbose = opt.verbose](
            const std::string& name, int total,
            const std::string& cmd, const std::string& wd) {
        ui.onTaskStarted(name, total);
        if (verbose) ui.onTaskVerbose(cmd, wd);
    });
    engine.onTaskFinished([&ui](const std::string& name, bool ok, double dur, const std::string& err) {
        ui.onTaskFinished(name, ok, dur, err);
    });
    engine.onBuildFinished([&ui](bool ok, double dur, bool interrupted) {
        ui.onBuildFinished(ok, dur, interrupted);
    });

    bool result = engine.run(opt.target);

    if (g_interrupted.load()) {
        return 130;
    }

    if (!opt.dryRun && useCache && !engine.wasNoop()) {
        engine.updateFileStates();
        cache.store(*state, buildFile, opt.config, opt.platform);
        if (!cache.save(cachePath)) LOG_WARN("failed to save cache after build");
    }

    return result ? 0 : 1;
}