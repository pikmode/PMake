#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

// ============================================================
// BATCH STAT — P1 оптимизация.
//
// На Windows делает ОДИН FindFirstFileW на директорию вместо
// N GetFileAttributesExW. 300 файлов в одной папке:
//   было:  ~30 ms (по одному)
//   стало: ~1-2 ms (всё разом)
//
// На POSIX — fallback через std::filesystem (там statx и так быстрый).
//
// Возвращает map: path → FileInfo. Ключи — те же строки, что во входе.
// Если файла нет — exists=false.
// ============================================================
struct FileInfo {
    bool exists = false;
    std::int64_t mtime = -1;
    std::int64_t size = 0;
};

std::unordered_map<std::string, FileInfo>
statBatch(const std::vector<std::string>& paths);

} // namespace pmake::util