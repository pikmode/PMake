#pragma once

#include <string_view>
#include <vector>

#include "core/value.h"

namespace pmake::core {

class State;
class Scope;

class Builtins {
public:
    static Value call(std::string_view name,
                      const std::vector<Value>& args,
                      State* state,
                      Scope* scope);
};

} // namespace pmake::core