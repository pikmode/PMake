#include "core/cache.h"
#include "deps/deps_log.h"
#include "util/logging.h" // NEW: для LOG_WARN

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pmake::core {

namespace {
constexpr std::uint32_t kMagic   = 0x504D4B45u;  // "PMKE" LE
constexpr std::uint32_t kVersion = 8u;
} // namespace

Cache::Cache() = default;

void Cache::store(const State& state, std::string_view /*mainFile*/,
                  std::string_view config, std::string_view platform) {
    data_.version = kVersion;
    data_.config = std::string(config);
    data_.platform = std::string(platform);

    data_.globalVars = state.globals();
    data_.rules.clear();
    data_.rules.reserve(state.rules().size());
    for (const auto& [name, rule] : state.rules()) data_.rules.push_back(rule);

    data_.builds = state.builds();
    data_.pools = state.pools();
    data_.defaultTargets = state.defaultTargets();
    data_.includedFilesList = state.includedFiles();

    data_.buildLocalVars.clear();
    data_.buildLocalVars.reserve(state.builds().size());
    for (const Build& build : state.builds()) {
        std::unordered_map<std::string, std::string> locals;
        if (build.scope) locals = build.scope->locals();
        data_.buildLocalVars.push_back(std::move(locals));
    }

    data_.fileStates = state.fileStates();
    data_.commandHashes = state.commandHashes();
}

bool Cache::save(std::string_view path) const {
    std::filesystem::path p{std::string(path)};
    std::error_code ec;
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        LOG_ERROR(std::format("Cannot open cache file for writing: {}", path));
        return false;
    }
    if (!write(f)) {
        LOG_ERROR("Cache write failed");
        return false;
    }
    return true;
}

bool Cache::load(std::string_view path) {
    std::ifstream f(std::filesystem::path(std::string(path)), std::ios::binary);
    if (!f) return false;
    if (!read(f)) {
        loaded_ = false;
        return false;
    }
    loaded_ = true;
    return true;
}

bool Cache::write(std::ostream& out) const {
    write_u32(out, kMagic);
    write_u32(out, kVersion);

    write_str(out, data_.config);
    write_str(out, data_.platform);

    write_u32(out, static_cast<std::uint32_t>(data_.globalVars.size()));
    for (const auto& [k, v] : data_.globalVars) {
        write_str(out, k);
        write_str(out, v);
    }

    write_u32(out, static_cast<std::uint32_t>(data_.rules.size()));
    for (const Rule& r : data_.rules) {
        write_str(out, r.name);
        write_str(out, r.command);
        write_str(out, r.description);
        write_str(out, r.depfile);
        write_str(out, r.deps);
        write_str(out, r.rspfile);
        write_str(out, r.rspfile_content);
        write_u8(out, r.generator ? 1 : 0);
        write_u8(out, r.restat ? 1 : 0);
        write_u8(out, r.pool ? 1 : 0);
    }

    write_u32(out, static_cast<std::uint32_t>(data_.builds.size()));
    for (std::size_t i = 0; i < data_.builds.size(); ++i) {
        const Build& b = data_.builds[i];
        write_strs(out, b.outputs);
        write_strs(out, b.inputs);
        write_strs(out, b.implicit);
        write_strs(out, b.order_only);
        write_str(out, b.rule_name);

        const auto& locals = data_.buildLocalVars[i];
        write_u32(out, static_cast<std::uint32_t>(locals.size()));
        for (const auto& [k, v] : locals) {
            write_str(out, k);
            write_str(out, v);
        }
    }

    write_u32(out, static_cast<std::uint32_t>(data_.pools.size()));
    for (const auto& [name, pool] : data_.pools) {
        write_str(out, name);
        write_u32(out, static_cast<std::uint32_t>(pool.depth));
    }

    write_strs(out, data_.defaultTargets);
    write_strs(out, data_.includedFilesList);

    write_u32(out, static_cast<std::uint32_t>(data_.fileStates.size()));
    for (const auto& [path, fs] : data_.fileStates) {
        write_str(out, path);
        write_i64(out, fs.mtime);
        write_i64(out, fs.size);
        write_str(out, fs.hash);
    }

    write_u32(out, static_cast<std::uint32_t>(data_.commandHashes.size()));
    for (const auto& [output, hash] : data_.commandHashes) {
        write_str(out, output);
        write_str(out, hash);
    }

    const auto& depsEntries = deps::DepsLog::instance().entries();
    write_u32(out, static_cast<std::uint32_t>(depsEntries.size()));
    for (const auto& [key, entry] : depsEntries) {
        write_str(out, entry.output);
        write_u32(out, static_cast<std::uint32_t>(entry.inputs.size()));
        for (const auto& in : entry.inputs) {
            write_str(out, in);
        }
        write_i64(out, entry.mtime);
    }

    return out.good();
}

bool Cache::read(std::istream& in) {
    std::uint32_t magic = 0, version = 0;
    if (!read_u32(in, magic) || magic != kMagic) {
        LOG_VERBOSE("Cache: bad magic, ignoring");
        return false;
    }
    if (!read_u32(in, version)) return false;
    data_.version = version;
    if (version != kVersion) {
        LOG_WARN(std::format("Cache version mismatch ({} != {}), ignoring", version, kVersion));
        return false;
    }

    if (!read_str(in, data_.config)) return false;
    if (!read_str(in, data_.platform)) return false;

    std::uint32_t count = 0;

    if (!read_u32(in, count)) return false;
    data_.globalVars.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string k, v;
        if (!read_str(in, k) || !read_str(in, v)) return false;
        data_.globalVars[k] = v;
    }

    if (!read_u32(in, count)) return false;
    data_.rules.clear();
    data_.rules.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Rule r;
        std::uint8_t gen = 0, restat = 0, pool = 0;
        if (!read_str(in, r.name)) return false;
        if (!read_str(in, r.command)) return false;
        if (!read_str(in, r.description)) return false;
        if (!read_str(in, r.depfile)) return false;
        if (!read_str(in, r.deps)) return false;
        if (!read_str(in, r.rspfile)) return false;
        if (!read_str(in, r.rspfile_content)) return false;
        if (!read_u8(in, gen) || !read_u8(in, restat) || !read_u8(in, pool)) return false;
        r.generator = (gen != 0);
        r.restat    = (restat != 0);
        r.pool      = (pool != 0);
        data_.rules.push_back(std::move(r));
    }

    if (!read_u32(in, count)) return false;
    data_.builds.clear();
    data_.buildLocalVars.clear();
    data_.builds.reserve(count);
    data_.buildLocalVars.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Build b;
        if (!read_strs(in, b.outputs)) return false;
        if (!read_strs(in, b.inputs)) return false;
        if (!read_strs(in, b.implicit)) return false;
        if (!read_strs(in, b.order_only)) return false;
        if (!read_str(in, b.rule_name)) return false;

        std::uint32_t localCount = 0;
        if (!read_u32(in, localCount)) return false;
        std::unordered_map<std::string, std::string> locals;
        for (std::uint32_t j = 0; j < localCount; ++j) {
            std::string k, v;
            if (!read_str(in, k) || !read_str(in, v)) return false;
            locals[k] = v;
        }
        data_.builds.push_back(std::move(b));
        data_.buildLocalVars.push_back(std::move(locals));
    }

    if (!read_u32(in, count)) return false;
    data_.pools.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string name;
        std::uint32_t depth = 0;
        if (!read_str(in, name)) return false;
        if (!read_u32(in, depth)) return false;
        data_.pools[name] = Pool{name, static_cast<int>(depth)};
    }

    if (!read_strs(in, data_.defaultTargets)) return false;
    if (!read_strs(in, data_.includedFilesList)) return false;

    if (!read_u32(in, count)) return false;
    data_.fileStates.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string path;
        std::int64_t mtime = -1, size = 0;
        std::string hash;
        if (!read_str(in, path)) return false;
        if (!read_i64(in, mtime)) return false;
        if (!read_i64(in, size)) return false;
        if (!read_str(in, hash)) return false;
        FileState fs;
        fs.mtime = mtime;
        fs.size = size;
        fs.hash = std::move(hash);
        data_.fileStates[path] = std::move(fs);
    }

    if (!read_u32(in, count)) return false;
    data_.commandHashes.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string output, hash;
        if (!read_str(in, output) || !read_str(in, hash)) return false;
        data_.commandHashes[output] = hash;
    }

    std::uint32_t depsCount = 0;
    if (!read_u32(in, depsCount)) return false;
    auto& depsLog = deps::DepsLog::instance();
    depsLog.clear();
    auto& depsMap = depsLog.entries();
    for (std::uint32_t i = 0; i < depsCount; ++i) {
        deps::DepsEntry entry;
        if (!read_str(in, entry.output)) return false;
        std::uint32_t inputCount = 0;
        if (!read_u32(in, inputCount)) return false;
        entry.inputs.clear();
        entry.inputs.reserve(inputCount);
        for (std::uint32_t j = 0; j < inputCount; ++j) {
            std::string depPath;
            if (!read_str(in, depPath)) return false;
            entry.inputs.push_back(std::move(depPath));
        }
        if (!read_i64(in, entry.mtime)) return false;
        entry.isLoaded = true;
        depsMap[entry.output] = std::move(entry);
    }

    return in.good();
}

bool Cache::isValid(std::string_view /*mainFile*/) const {
    return loaded_;
}

std::unique_ptr<State> Cache::restore() const {
    auto state = std::make_unique<State>();

    for (const auto& [k, v] : data_.globalVars) state->setGlobal(k, v);
    for (const Rule& rule : data_.rules) state->addRule(rule);

    for (std::size_t i = 0; i < data_.builds.size(); ++i) {
        Build build = data_.builds[i];
        auto scope = std::make_shared<Scope>();
        for (const auto& [k, v] : data_.buildLocalVars[i]) scope->set(k, v);
        build.scope = scope;
        state->addBuild(build);
    }

    for (const auto& [name, pool] : data_.pools) state->addPool(pool);

    state->defaultTargets() = data_.defaultTargets;
    state->includedFiles()  = data_.includedFilesList;

    for (const auto& [path, fs] : data_.fileStates) state->setFileState(path, fs);

    state->setCommandHashes(data_.commandHashes);

    return state;
}

void Cache::write_u32(std::ostream& out, std::uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void Cache::write_u64(std::ostream& out, std::uint64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void Cache::write_i64(std::ostream& out, std::int64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void Cache::write_u8(std::ostream& out, std::uint8_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void Cache::write_str(std::ostream& out, std::string_view s) {
    write_u32(out, static_cast<std::uint32_t>(s.size()));
    if (!s.empty()) out.write(s.data(), static_cast<std::streamsize>(s.size()));
}
void Cache::write_strs(std::ostream& out, const std::vector<std::string>& v) {
    write_u32(out, static_cast<std::uint32_t>(v.size()));
    for (const auto& s : v) write_str(out, s);
}

bool Cache::read_u32(std::istream& in, std::uint32_t& v) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof(v)));
}
bool Cache::read_u64(std::istream& in, std::uint64_t& v) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof(v)));
}
bool Cache::read_i64(std::istream& in, std::int64_t& v) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof(v)));
}
bool Cache::read_u8(std::istream& in, std::uint8_t& v) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof(v)));
}
bool Cache::read_str(std::istream& in, std::string& s) {
    std::uint32_t n = 0;
    if (!read_u32(in, n)) return false;
    if (n > 64 * 1024 * 1024) return false; 
    s.assign(n, '\0');
    if (n > 0 && !in.read(s.data(), static_cast<std::streamsize>(n))) return false;
    return true;
}
bool Cache::read_strs(std::istream& in, std::vector<std::string>& v) {
    std::uint32_t n = 0;
    if (!read_u32(in, n)) return false;
    if (n > 64 * 1024 * 1024) return false; 
    v.clear();
    v.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        std::string s;
        if (!read_str(in, s)) return false;
        v.push_back(std::move(s));
    }
    return true;
}

} // namespace pmake::core