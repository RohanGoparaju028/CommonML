#pragma once

#include "cml/type.hpp"
#include "cml/value.hpp"

#include <string>
#include <vector>

namespace cml {

struct BuiltinDef {
    std::string name;
    TypeScheme scheme;
    size_t arity = 1;
    NativeFn fn;
};

const std::vector<BuiltinDef>& get_builtins();

}  // namespace cml
