#pragma once

#include "cml/pipeline.hpp"

#include <iostream>
#include <string>

namespace cml {

struct CompileOutputOptions {
    std::string input_file;
    std::string output_file;
    bool emit_bc_only = false;
    std::string fallback_runtime;
    EvalOptions eval_opts;
};

std::string resolve_runtime_binary(const std::string& argv0, const std::string& compile_fallback);

int compile_file_to_output(const CompileOutputOptions& opts, std::ostream& out, std::ostream& err);

}  // namespace cml
