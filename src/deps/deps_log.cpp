#include "deps/deps_log.h"
#include "util/filesystem.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pmake::deps {

DepsLog& DepsLog::instance() {
    static DepsLog log;
    return log;
}

std::unordered_map<std::string, DepsEntry>& DepsLog::entries() {
    return entries_;
}

const std::unordered_map<std::string, DepsEntry>& DepsLog::entries() const {
    return entries_;
}

// ============================================================
// StringPool
// ============================================================
std::uint32_t DepsLog::intern(std::string_view s) {
    // Хэш-поиск по string_view — без аллокации std::string при попадании
    auto it = strToId_.find(s);
    if (it != strToId_.end()) return it->second;

    std::uint32_t id = static_cast<std::uint32_t>(strings_.size());
    strings_.emplace_back(s);
    // string_view на strings_.back() — стабилен (deque не реаллоцирует)
    strToId_.emplace(strings_.back(), id);
    return id;
}

std::string_view DepsLog::lookup(std::uint32_t id) const {
    if (id >= strings_.size()) return {};
    return strings_[id];
}

// ============================================================
// Read API
// ============================================================
std::vector<std::string> DepsLog::inputsFor(std::string_view output) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(std::string(output));
    if (it == entries_.end()) return {};

    std::vector<std::string> result;
    result.reserve(it->second.inputIds.size());
    for (std::uint32_t id : it->second.inputIds) {
        result.emplace_back(lookup(id));
    }
    return result;
}

const std::vector<std::uint32_t>* DepsLog::inputIdsFor(std::string_view output) const {
    auto it = entries_.find(std::string(output));
    if (it == entries_.end()) return nullptr;
    return &it->second.inputIds;
}

// ============================================================
// Write API
// ============================================================
void DepsLog::record(std::string_view output, const std::vector<std::string>& inputs) {
    std::lock_guard<std::mutex> lock(mutex_);

    DepsEntry& e = entries_[std::string(output)];
    e.output = std::string(output);
    e.inputIds.clear();
    e.inputIds.reserve(inputs.size());
    for (const auto& in : inputs) {
        e.inputIds.push_back(intern(in));
    }
    e.isLoaded = true;

    std::int64_t mt = -1, sz = 0;
    if (util::statFileFast(output, mt, sz)) e.mtime = mt;
    else e.mtime = 0;
}

void DepsLog::restore(std::string_view output,
                      const std::vector<std::string>& inputs,
                      std::int64_t mtime) {
    std::lock_guard<std::mutex> lock(mutex_);

    DepsEntry& e = entries_[std::string(output)];
    e.output = std::string(output);
    e.inputIds.clear();
    e.inputIds.reserve(inputs.size());
    for (const auto& in : inputs) {
        e.inputIds.push_back(intern(in));
    }
    e.mtime = mtime;
    e.isLoaded = true;
}

void DepsLog::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    strings_.clear();
    strToId_.clear();
}

} // namespace pmake::deps