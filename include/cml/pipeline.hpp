#pragma once

#include "cml/ast.hpp"
#include "cml/compiler.hpp"
#include "cml/macro_expander.hpp"
#include "cml/source.hpp"
#include "cml/typecheck.hpp"
#include "cml/vm.hpp"

#include <string>
#include <vector>

namespace cml {

struct EvalOptions {
    bool dump_tokens = false;
    bool dump_ast = false;
    bool dump_expanded = false;
    bool dump_types = false;
    bool dump_bytecode = false;
    bool typecheck_only = false;
};

struct EvalResult {
    Value value = Value::unit();
    std::string type_str;
    std::vector<std::string> stmt_types;
    std::string debug_output;
};

class Pipeline {
public:
    Pipeline();

    EvalResult eval_source(const std::string& filename, const std::string& code,
                           const EvalOptions& opts = {});
    EvalResult eval_expr(const std::string& filename, const std::string& code,
                         const EvalOptions& opts = {});

    const TypeChecker& typechecker() const { return typechecker_; }
    const VM& vm() const { return vm_; }

private:
    MacroExpander expander_;
    TypeChecker typechecker_;
    VM vm_;
};

}  // namespace cml
