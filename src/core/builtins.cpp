#include "core/builtins.h"
#include "core/state.h"
#include "deps/dep_parser.h"        // NEW: для dep_inputs
#include "util/filesystem.h"
#include "util/hash.h"
#include "util/string_utils.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace pmake::core {

namespace fs = std::filesystem;

namespace {

bool glob_match(std::string_view pattern, std::string_view str) {
    std::size_t p = 0, s = 0;
    std::size_t star_p = std::string_view::npos, star_s = 0;
    while (s < str.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == str[s])) {
            ++p; ++s;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_s = s;
        } else if (star_p != std::string_view::npos) {
            p = star_p + 1;
            s = ++star_s;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

std::vector<std::string> flatten_args(const std::vector<Value>& args) {
    std::vector<std::string> out;
    for (const auto& v : args) {
        if (v.is_list()) {
            for (const auto& item : *v.as_list()) out.push_back(item.to_string());
        } else {
            out.push_back(v.to_string());
        }
    }
    return out;
}

std::string absolute_path_str(const fs::path& p) {
    if (p.is_absolute()) return p.string();
    std::error_code ec;
    auto a = fs::absolute(p, ec);
    return ec ? p.string() : a.string();
}

} // namespace

// ============================================================
// NEW: dep_inputs(path) — парсит .d-файл через DepParser и возвращает
// space-joined строку inputs (без target).
//
// Пример:
//   inputs = dep_inputs("build/main.o.d")
//   rule r { command = "cc ${inputs}" }
//
// Полезно и для тестов, и для дебага depfile'ов.
// ============================================================
static Value depInputsFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    auto inputs = deps::DepParser::parse(args[0].to_string());
    std::string joined;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (i) joined += ' ';
        joined += inputs[i];
    }
    return Value(std::move(joined));
}

static Value globFunction(const std::vector<Value>& args, State* state) {
    if (args.empty()) return Value();
    std::string pattern = args[0].to_string();
    std::string baseDir;
    if (args.size() > 1) {
        baseDir = args[1].to_string();
    } else {
        auto projDir = state->getGlobal("project_directory");
        if (projDir.has_value()) baseDir = *projDir;
        else {
            std::error_code ec;
            baseDir = fs::current_path(ec).string();
        }
    }

    ValueList result;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(baseDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (glob_match(pattern, name)) {
            result.emplace_back(absolute_path_str(entry.path()));
        }
    }
    return Value(std::move(result));
}

static Value replaceSuffixFunction(const std::vector<Value>& args) {
    if (args.size() < 2) return Value();
    std::string newSuffix = args[1].to_string();
    std::vector<std::string> items = flatten_args({args[0]});
    ValueList result;
    for (const auto& s : items) {
        fs::path p(s);
        auto parent = p.parent_path();
        std::string dir = parent.is_absolute() ? parent.string() : absolute_path_str(parent);
        std::string base = p.stem().string();
        if (dir.empty() || dir == ".") {
            result.emplace_back(base + newSuffix);
        } else {
            result.emplace_back(dir + "/" + base + newSuffix);
        }
    }
    return Value(std::move(result));
}

static Value joinFunction(const std::vector<Value>& args) {
    if (args.size() < 2) return Value();
    std::vector<std::string> parts = flatten_args(args);
    if (parts.empty()) return Value();
    std::string glue = parts.back();
    parts.pop_back();
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) result += glue;
        result += parts[i];
    }
    return Value(std::move(result));
}

static Value relativePathFunction(const std::vector<Value>& args) {
    if (args.size() < 2) return Value();
    std::error_code ec;
    auto rel = fs::relative(fs::path(args[0].to_string()),
                            fs::path(args[1].to_string()), ec);
    return Value(ec ? args[0].to_string() : rel.string());
}

static Value basenameFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    return Value(fs::path(args[0].to_string()).filename().string());
}

static Value dirnameFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    auto p = fs::path(args[0].to_string()).parent_path();
    return Value(p.empty() ? std::string(".") : absolute_path_str(p));
}

static Value envFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    const char* v = std::getenv(args[0].to_string().c_str());
    return v ? Value(std::string(v)) : Value();
}

static Value existsFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value(false);
    return Value(util::fileExists(args[0].to_string()));
}

static Value fileMtimeFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value(-1.0);
    return Value(static_cast<double>(util::getFileMtime(args[0].to_string())));
}

static Value readFileFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    std::string data;
    if (util::readFile(args[0].to_string(), data)) return Value(std::move(data));
    return Value();
}

static Value extensionFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    auto ext = fs::path(args[0].to_string()).extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    return Value(std::move(ext));
}

static Value filenameFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    return Value(fs::path(args[0].to_string()).filename().string());
}

static Value splitFunction(const std::vector<Value>& args) {
    if (args.size() < 2) return Value();
    std::string str = args[0].to_string();
    std::string delim = args[1].to_string();
    ValueList result;
    if (delim.empty()) {
        for (char c : str) result.emplace_back(std::string(1, c));
        return Value(std::move(result));
    }
    std::size_t start = 0;
    while (start <= str.size()) {
        auto pos = str.find(delim, start);
        if (pos == std::string::npos) {
            result.emplace_back(str.substr(start));
            break;
        }
        result.emplace_back(str.substr(start, pos - start));
        start = pos + delim.size();
    }
    return Value(std::move(result));
}

static Value replaceFunction(const std::vector<Value>& args) {
    if (args.size() < 3) return Value();
    std::string s = args[0].to_string();
    std::string from = args[1].to_string();
    std::string to = args[2].to_string();
    if (!from.empty()) {
        std::size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return Value(std::move(s));
}

static Value startsWithFunction(const std::vector<Value>& args) {
    if (args.size() < 2) return Value(false);
    std::string s = args[0].to_string();
    std::string p = args[1].to_string();
    return Value(s.size() >= p.size() && s.compare(0, p.size(), p) == 0);
}

static Value endsWithFunction(const std::vector<Value>& args) {
    if (args.size() < 2) return Value(false);
    std::string s = args[0].to_string();
    std::string suf = args[1].to_string();
    return Value(s.size() >= suf.size() &&
                 s.compare(s.size() - suf.size(), suf.size(), suf) == 0);
}

static Value containsFunction(const std::vector<Value>& args) {
    if (args.size() < 2) return Value(false);
    return Value(args[0].to_string().find(args[1].to_string()) != std::string::npos);
}

static Value cwdFunction() {
    std::error_code ec;
    return Value(fs::current_path(ec).string());
}

static Value absolutePathFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    return Value(absolute_path_str(fs::path(args[0].to_string())));
}

static Value hashFunction(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    return Value(util::sha1_hex(args[0].to_string()));
}

static Value writeFileFunction(const std::vector<Value>& args) {
    if (args.size() < 2) return Value(false);
    return Value(util::writeFile(args[0].to_string(), args[1].to_string()));
}

Value Builtins::call(std::string_view name,
                     const std::vector<Value>& args,
                     State* state,
                     Scope* scope) {
    (void)scope;
    if (name == "dep_inputs")     return depInputsFunction(args);     // NEW
    if (name == "glob")           return globFunction(args, state);
    if (name == "replace_suffix") return replaceSuffixFunction(args);
    if (name == "join")           return joinFunction(args);
    if (name == "relative_path")  return relativePathFunction(args);
    if (name == "basename")       return basenameFunction(args);
    if (name == "dirname")        return dirnameFunction(args);
    if (name == "env")            return envFunction(args);
    if (name == "exists")         return existsFunction(args);
    if (name == "file_mtime")     return fileMtimeFunction(args);
    if (name == "read_file")      return readFileFunction(args);
    if (name == "extension")      return extensionFunction(args);
    if (name == "filename")       return filenameFunction(args);
    if (name == "split")          return splitFunction(args);
    if (name == "replace")        return replaceFunction(args);
    if (name == "starts_with")    return startsWithFunction(args);
    if (name == "ends_with")      return endsWithFunction(args);
    if (name == "contains")       return containsFunction(args);
    if (name == "cwd")            return cwdFunction();
    if (name == "absolute_path")  return absolutePathFunction(args);
    if (name == "hash")           return hashFunction(args);
    if (name == "write_file")     return writeFileFunction(args);

    std::println(std::cerr, "Unknown builtin function: {}", name);
    return Value();
}

} // namespace pmake::core