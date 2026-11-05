#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace pmake::util {

class StringPool {
public:
    static StringPool& instance() {
        static StringPool pool;
        return pool;
    }

    int intern(std::string_view str) {
        auto it = map_.find(std::string(str));
        if (it != map_.end()) return it->second;
        int id = static_cast<int>(strings_.size());
        strings_.emplace_back(str);
        map_.emplace(std::string(str), id);
        return id;
    }

    int intern(const char* str) { return intern(std::string_view(str)); }

    std::string_view get(int id) const {
        if (id < 0 || id >= static_cast<int>(strings_.size())) return {};
        return strings_[static_cast<std::size_t>(id)];
    }

    std::size_t size() const { return strings_.size(); }

    void clear() {
        strings_.clear();
        map_.clear();
    }

private:
    StringPool() = default;
    std::deque<std::string> strings_;
    std::unordered_map<std::string, int> map_;
};

} // namespace pmake::util