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

std::vector<std::string> DepsLog::inputsFor(std::string_view output) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(std::string(output));
    if (it == entries_.end()) return {};
    return it->second.inputs;
}

void DepsLog::record(std::string_view output, const std::vector<std::string>& inputs) {
    std::lock_guard<std::mutex> lock(mutex_);
    DepsEntry& e = entries_[std::string(output)];
    e.output = std::string(output);
    e.inputs = inputs;
    e.isLoaded = true;
    std::int64_t mt = -1, sz = 0;
    if (util::statFileFast(output, mt, sz)) e.mtime = mt;
    else e.mtime = 0;
}

void DepsLog::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

} // namespace pmake::deps