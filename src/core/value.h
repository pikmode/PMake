#pragma once

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pmake::core {

struct Value;
using ValueList = std::vector<Value>;

struct Value {
    std::variant<std::monostate, bool, double, std::string,
                 std::shared_ptr<ValueList>> data;

    Value() : data(std::monostate{}) {}
    Value(bool b) : data(b) {}
    Value(double d) : data(d) {}
    Value(int i) : data(static_cast<double>(i)) {}
    Value(std::string s) : data(std::move(s)) {}
    Value(std::string_view s) : data(std::string(s)) {}
    Value(const char* s) : data(std::string(s)) {}
    Value(ValueList list) : data(std::make_shared<ValueList>(std::move(list))) {}

    bool is_null()   const { return std::holds_alternative<std::monostate>(data); }
    bool is_bool()   const { return std::holds_alternative<bool>(data); }
    bool is_number() const { return std::holds_alternative<double>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_list()   const { return std::holds_alternative<std::shared_ptr<ValueList>>(data); }

    bool             as_bool()   const { return is_bool()   ? std::get<bool>(data) : false; }
    double           as_number() const { return is_number() ? std::get<double>(data) : 0.0; }
    std::string_view as_string() const {
        return is_string() ? std::string_view(std::get<std::string>(data))
                           : std::string_view{};
    }
    const ValueList* as_list() const {
        if (!is_list()) return nullptr;
        return std::get<std::shared_ptr<ValueList>>(data).get();
    }

    std::string to_string() const {
        if (is_string()) return std::get<std::string>(data);
        if (is_number()) return std::format("{}", std::get<double>(data));
        if (is_bool())   return std::get<bool>(data) ? "true" : "false";
        return {};
    }

    bool to_bool() const {
        if (is_bool())   return std::get<bool>(data);
        if (is_number()) return std::get<double>(data) != 0.0;
        if (is_string()) {
            const auto& s = std::get<std::string>(data);
            return s == "true" || s == "1" || s == "yes" || s == "on";
        }
        return false;
    }
};

} // namespace pmake::core