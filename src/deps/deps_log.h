#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pmake::deps {

struct DepsEntry {
    std::string output;
    std::vector<std::string> inputs;
    std::int64_t mtime = 0;
    bool isLoaded = false;
};

class DepsLog {
public:
    static DepsLog& instance();

    std::unordered_map<std::string, DepsEntry>& entries();
    const std::unordered_map<std::string, DepsEntry>& entries() const;

    std::vector<std::string> inputsFor(std::string_view output) const;
    void record(std::string_view output, const std::vector<std::string>& inputs);
    void clear();

private:
    DepsLog() = default;
    std::unordered_map<std::string, DepsEntry> entries_;
    mutable std::mutex mutex_;
};

} // namespace pmake::deps