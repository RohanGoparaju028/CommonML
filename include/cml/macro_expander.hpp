#pragma once

#include "cml/ast.hpp"
#include "cml/vm.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace cml {

struct MacroDef {
    Ident name;
    std::vector<Ident> params;
    ExprPtr body;
    std::shared_ptr<FunctionProto> compiled_fn;
};

class MacroExpander {
public:
    MacroExpander();

    Program expand_program(const Program& prog);
    StmtPtr expand_stmt(const StmtPtr& stmt);
    ExprPtr expand_expr(const ExprPtr& expr, size_t depth = 0);

    bool has_macro(const std::string& name) const;

private:
    void register_macro(const StmtMacro& macro, const SourceSpan& span);
    PatternPtr expand_pattern(const PatternPtr& pat, size_t depth);
    ExprPtr invoke_macro(const MacroDef& def, const ExprMacroCall& call, size_t depth);

    ScopeId next_scope_ = 1;
    ScopeEnv<MacroDef> macros_;
    VM macro_vm_;
};

}  // namespace cml
