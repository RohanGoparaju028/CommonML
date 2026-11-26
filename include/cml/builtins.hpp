#pragma once

#include "cml/type.hpp"
#include "cml/value.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace cml {

struct ProgramExit : std::exception {
    int code = 0;
    explicit ProgramExit(int c) : code(c) {}
    const char* what() const noexcept override { return "program exit"; }
};

void set_cli_args(std::vector<std::string> args);
const std::vector<std::string>& get_cli_args();

struct BuiltinDef {
    std::string name;
    TypeScheme scheme;
    size_t arity = 1;
    NativeFn fn;
};

const std::vector<BuiltinDef>& get_builtins();

}  // namespace cml
