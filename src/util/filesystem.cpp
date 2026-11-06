#include "util/filesystem.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <filesystem>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/stat.h>
#endif

namespace pmake::util {

namespace {

#ifdef _WIN32
std::wstring utf8_to_utf16(std::string_view s) {
    if (s.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                   static_cast<int>(s.size()), nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring w(static_cast<std::size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(),
                        static_cast<int>(s.size()), w.data(), wlen);
    return w;
}

std::string utf16_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                  static_cast<int>(w.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        s.data(), len, nullptr, nullptr);
    return s;
}

std::int64_t filetime_to_unix_ms(const FILETIME& ft) {
    ULARGE_INTEGER t;
    t.LowPart  = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>(t.QuadPart / 10000ULL) - 11644473600000LL;
}
#endif

std::filesystem::path to_path(std::string_view p) {
#ifdef _WIN32
    return std::filesystem::path(utf8_to_utf16(p));
#else
    return std::filesystem::path(std::string(p));
#endif
}

// Разбивает "dir/sub/file.ext" на (dir/sub, file.ext).
// Возвращает {parent, name}. Если parent пуст — значит файл в текущей папке.
std::pair<std::string, std::string> split_path(std::string_view p) {
    auto pos_slash = p.find_last_of("/\\");
    if (pos_slash == std::string_view::npos) {
        return {".", std::string(p)};
    }
    std::string parent(p.substr(0, pos_slash));
    std::string name(p.substr(pos_slash + 1));
    if (parent.empty()) parent = "/";
    return {parent, name};
}

} // namespace

FileCache& FileCache::instance() {
    static FileCache f;
    return f;
}

std::int64_t FileCache::getMtime(std::string_view path) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = cache_.find(std::string(path));
    if (it != cache_.end()) return it->second;
    std::int64_t mtime = -1, size = 0;
    cache_[std::string(path)] = statFileFast(path, mtime, size) ? mtime : -1;
    return cache_[std::string(path)];
}

void FileCache::touch(std::string_view path) {
    std::lock_guard<std::mutex> lk(mutex_);
    std::int64_t mtime = -1, size = 0;
    if (statFileFast(path, mtime, size)) cache_[std::string(path)] = mtime;
}

void FileCache::setMtime(std::string_view path, std::int64_t mtime) {
    std::lock_guard<std::mutex> lk(mutex_);
    cache_[std::string(path)] = mtime;
}

void FileCache::invalidate(std::string_view path) {
    std::lock_guard<std::mutex> lk(mutex_);
    cache_.erase(std::string(path));
}

void FileCache::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    cache_.clear();
}

bool statFileFast(std::string_view path, std::int64_t& mtime, std::int64_t& size) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attr;
    std::wstring wpath = utf8_to_utf16(path);
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr)) {
        mtime = -1; size = 0; return false;
    }
    mtime = filetime_to_unix_ms(attr.ftLastWriteTime);
    ULARGE_INTEGER s;
    s.LowPart  = attr.nFileSizeLow;
    s.HighPart = attr.nFileSizeHigh;
    size = static_cast<std::int64_t>(s.QuadPart);
    return true;
#else
    struct stat st;
    std::string p(path);
    if (::stat(p.c_str(), &st) != 0) { mtime = -1; size = 0; return false; }
    mtime = static_cast<std::int64_t>(st.st_mtime) * 1000LL;
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
    mtime += st.st_mtim.tv_nsec / 1000000LL;
#elif defined(__APPLE__)
    mtime += st.st_mtimespec.tv_nsec / 1000000LL;
#endif
    size = static_cast<std::int64_t>(st.st_size);
    return true;
#endif
}

bool fileExists(std::string_view path) {
    std::int64_t mtime = -1, size = 0;
    return statFileFast(path, mtime, size);
}

std::int64_t getFileMtime(std::string_view path) {
    return FileCache::instance().getMtime(path);
}

bool readFile(std::string_view path, std::string& out) {
    std::ifstream f(to_path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(std::string_view path, std::string_view data) {
    std::ofstream f(to_path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!f) return false;
    FileCache::instance().touch(path);
    return true;
}

bool ensureDirectory(std::string_view path) {
    if (path.empty()) return true;
    std::error_code ec;
    auto p = to_path(path);
    if (std::filesystem::exists(p, ec)) return true;
    std::filesystem::create_directories(p, ec);
    return !ec;
}

// ============================================================
// statBatch — P1.
// ============================================================
std::unordered_map<std::string, FileInfo>
statBatch(const std::vector<std::string>& paths) {
    std::unordered_map<std::string, FileInfo> result;
    if (paths.empty()) return result;

#ifdef _WIN32
    // 1. Группируем по директориям: { "obj/": ["obj/a.o", "obj/b.o"] }
    std::unordered_map<std::string, std::vector<std::string>> by_dir;
    for (const auto& p : paths) {
        auto [dir, name] = split_path(p);
        by_dir[dir].push_back(p);
    }

    // 2. Для каждой директории — ОДИН FindFirstFileW("*")
    for (const auto& [dir, files] : by_dir) {
        // Ищем один раз на директорию.
        std::wstring wdir = utf8_to_utf16(dir);
        if (wdir.empty()) continue;
        if (wdir.back() != L'\\' && wdir.back() != L'/') wdir += L'\\';
        std::wstring pattern = wdir + L"*";

        WIN32_FIND_DATAW data{};
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &data);
        if (hFind == INVALID_HANDLE_VALUE) {
            // Директории нет — все файлы в ней "не существуют".
            for (const auto& f : files) {
                result[f] = FileInfo{};
            }
            continue;
        }

        // Сначала складываем всё, что нашли, по имени.
        std::unordered_map<std::string, FileInfo> dir_cache;
        do {
            std::string name = utf16_to_utf8(data.cFileName);
            if (name == "." || name == "..") continue;

            FileInfo fi;
            fi.exists = true;
            fi.mtime  = filetime_to_unix_ms(data.ftLastWriteTime);
            ULARGE_INTEGER s;
            s.LowPart  = data.nFileSizeLow;
            s.HighPart = data.nFileSizeHigh;
            fi.size = static_cast<std::int64_t>(s.QuadPart);
            dir_cache[name] = fi;
        } while (FindNextFileW(hFind, &data));
        FindClose(hFind);

        // Проставляем каждому запрошенному файлу.
        for (const auto& f : files) {
            auto [d, name] = split_path(f);
            auto it = dir_cache.find(name);
            if (it != dir_cache.end()) result[f] = it->second;
            else                       result[f] = FileInfo{};  // не существует
        }
    }
#else
    // POSIX — просто цикл stat(). На Linux getdents64 + statx
    // всё равно даёт хорошую скорость, оптимизировать не нужно.
    for (const auto& p : paths) {
        FileInfo fi;
        if (statFileFast(p, fi.mtime, fi.size)) fi.exists = true;
        result[p] = fi;
    }
#endif

    return result;
}

} // namespace pmake::util