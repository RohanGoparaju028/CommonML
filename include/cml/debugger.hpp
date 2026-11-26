#pragma once

#include "cml/vm.hpp"

#include <istream>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace cml {

struct DebugSessionConfig {
    std::string entry_file;
    std::vector<std::string> args;
    std::vector<size_t> initial_breakpoints;
    bool stop_on_entry = true;
};

class Debugger {
public:
    int run_cli(const DebugSessionConfig& cfg, std::istream& in, std::ostream& out,
                std::ostream& err);
    int run_dap(std::istream& in, std::ostream& out, std::ostream& err);
};

}  // namespace cml
