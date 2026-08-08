#pragma once

#include <unordered_map>
#include <utility>

namespace pmake::util {

template<typename Key, typename Value>
class HashMap : public std::unordered_map<Key, Value> {
public:
    explicit HashMap(size_t reserve_size = 1024) {
        this->reserve(reserve_size);
    }
    // Дополнительные методы при необходимости
};

} // namespace pmake::util