#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pmake::deps {

// CHANGED (Issue #1 RAM): inputs → inputIds.
//
// Было: vector<string> (500 headers × 32 B = 16 KB на entry).
//       На Chromium: 350K × 16 KB = 5.6 GB.
//
// Стало: vector<uint32_t> (500 × 4 B = 2 KB на entry) + StringPool
//        (50K уникальных headers × 80 B = 4 MB).
//        На Chromium: 350K × 2 KB + 4 MB = ~700 MB. ×8 меньше.
//
// Каждый id — индекс в DepsLog::strings_ (deque<string>).
// string_view из deque стабилен при push_back — deque не реаллоцирует
// существующие элементы.
struct DepsEntry {
    std::string output;
    std::vector<std::uint32_t> inputIds;   // CHANGED: было inputs
    std::int64_t mtime = 0;
    bool isLoaded = false;
};

class DepsLog {
public:
    static DepsLog& instance();

    std::unordered_map<std::string, DepsEntry>& entries();
    const std::unordered_map<std::string, DepsEntry>& entries() const;

    // Раскрывает id → строки. Копия (для совместимости: node_builder.cpp,
    // depsListsEqual, встроенный dep_inputs).
    std::vector<std::string> inputsFor(std::string_view output) const;

    // NEW: без mutex, без копии — для Engine::collectDirtyNodes prefetch.
    // Контракт: только main-thread, ДО старта worker'ов, не держать дольше.
    const std::vector<std::uint32_t>* inputIdsFor(std::string_view output) const;

    // NEW: id → string_view. Стабилен пока никто не вызвал clear().
    // Без mutex — deque гарантирует стабильность string_view при push_back.
    std::string_view lookup(std::uint32_t id) const;

    void record(std::string_view output, const std::vector<std::string>& inputs);

    // NEW: для Cache::restore — восстановить entry с сохранённым mtime
    // (не перезаписывать statFileFast'ом, как record делает).
    void restore(std::string_view output,
                 const std::vector<std::string>& inputs,
                 std::int64_t mtime);

    void clear();

private:
    DepsLog() = default;

    // Интернирует строку. Возвращает id (новый или существующий).
    // Приватный — используется record() и restore().
    std::uint32_t intern(std::string_view s);

    std::unordered_map<std::string, DepsEntry> entries_;

    // StringPool: один экземпляр каждого уникального header-path.
    // deque гарантирует стабильность string_view при push_back.
    std::deque<std::string> strings_;
    std::unordered_map<std::string_view, std::uint32_t> strToId_;

    mutable std::mutex mutex_;
};

} // namespace pmake::deps