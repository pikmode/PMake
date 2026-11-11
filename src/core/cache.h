#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/state.h"

namespace pmake::deps { class DepsLog; }

namespace pmake::core {

class Cache {
public:
    Cache();

    bool save(std::string_view path) const;
    bool load(std::string_view path);

    bool isValid(std::string_view mainFile = {}) const;

    // CHANGED: Добавлены параметры config и platform
    void store(const State& state, std::string_view mainFile = {}, 
               std::string_view config = {}, 
               std::string_view platform = {});
               
    std::unique_ptr<State> restore() const;

    const std::unordered_map<std::string, FileState>& fileStates() const { return data_.fileStates; }
    const std::unordered_map<std::string, std::string>& commandHashes() const { return data_.commandHashes; }

    // NEW: Геттеры для проверки в main.cpp
    const std::string& getConfig() const { return data_.config; }
    const std::string& getPlatform() const { return data_.platform; }

private:
    struct Data {
        std::uint32_t version = 8; // ВЕРСИЯ ПОДНЯТА С 7 НА 8
        std::string config;        // NEW
        std::string platform;      // NEW
        std::unordered_map<std::string, std::string> globalVars;
        std::vector<Rule> rules;
        std::vector<Build> builds;
        std::vector<std::unordered_map<std::string, std::string>> buildLocalVars;
        std::unordered_map<std::string, Pool> pools;
        std::vector<std::string> defaultTargets;
        std::vector<std::string> includedFilesList;
        std::unordered_map<std::string, FileState> fileStates;
        std::unordered_map<std::string, std::string> commandHashes;
    };

    Data data_;
    bool loaded_ = false;

    bool write(std::ostream& out) const;
    bool read(std::istream& in);

    // helpers для бинарной сериализации
    static void write_u32(std::ostream& out, std::uint32_t v);
    static void write_u64(std::ostream& out, std::uint64_t v);
    static void write_i64(std::ostream& out, std::int64_t v);
    static void write_u8(std::ostream& out, std::uint8_t v);
    static void write_str(std::ostream& out, std::string_view s);
    static void write_strs(std::ostream& out, const std::vector<std::string>& v);

    static bool read_u32(std::istream& in, std::uint32_t& v);
    static bool read_u64(std::istream& in, std::uint64_t& v);
    static bool read_i64(std::istream& in, std::int64_t& v);
    static bool read_u8(std::istream& in, std::uint8_t& v);
    static bool read_str(std::istream& in, std::string& s);
    static bool read_strs(std::istream& in, std::vector<std::string>& v);
};

} // namespace pmake::core