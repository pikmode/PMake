#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace pmake::util {

class FileCache {
public:
    static FileCache& instance();
    std::int64_t getMtime(std::string_view path);
    void touch(std::string_view path);
    void setMtime(std::string_view path, std::int64_t mtime);
    void invalidate(std::string_view path);
    void clear();

private:
    std::unordered_map<std::string, std::int64_t> cache_;
    std::mutex mutex_;
};

bool fileExists(std::string_view path);
std::int64_t getFileMtime(std::string_view path);
bool statFileFast(std::string_view path, std::int64_t& mtime, std::int64_t& size);
bool readFile(std::string_view path, std::string& out);
bool writeFile(std::string_view path, std::string_view data);
bool ensureDirectory(std::string_view path);

} // namespace pmake::util