#pragma once

#include "cml/ast.hpp"
#include "cml/source.hpp"
#include "cml/type.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cml {

struct CtorInfo {
    std::string type_name;
    uint32_t tag = 0;
    size_t arity = 0;
    TypeScheme scheme;
};

struct TypeDeclInfo {
    std::string name;
    size_t arity = 0;
    std::vector<std::string> ctors;
};

struct StructInfo {
    std::string name;
    std::vector<uint32_t> type_params;
    std::vector<std::pair<std::string, TypePtr>> fields;
};

struct ModuleExports {
    std::string module_id;
    std::unordered_map<std::string, TypeScheme> values;
    std::unordered_map<std::string, StructInfo> structs;
    std::unordered_map<std::string, CtorInfo> ctors;
    std::unordered_map<std::string, TypeDeclInfo> type_decls;
};

class TypeChecker {
public:
    using ModuleResolverFn = std::function<ModuleExports(const std::string&, const SourceSpan&)>;

    TypeChecker();

    void register_builtins();
    void set_module_resolver(ModuleResolverFn fn) { module_resolver_ = std::move(fn); }
    void register_struct_info(const StructInfo& info);

    std::vector<std::string> check_program(const Program& prog);
    std::string check_stmt(const StmtPtr& stmt);
    TypePtr infer_expr(const ExprPtr& expr);

    const ScopeEnv<TypeScheme>& env() const { return env_; }
    const std::unordered_map<std::string, CtorInfo>& ctors() const { return ctors_; }
    const std::unordered_map<std::string, TypeDeclInfo>& type_decls() const { return type_decls_; }
    const std::unordered_map<std::string, StructInfo>& structs() const { return structs_; }
    const std::unordered_map<std::string, ModuleExports>& modules() const { return modules_; }
    const std::vector<std::string>& exported_names() const { return exported_names_; }

private:
    struct PatResult {
        TypePtr type;
        std::vector<std::pair<Ident, TypePtr>> bindings;
    };

    void register_type_decl(const StmtTypeDecl& decl, const SourceSpan& span);
    void register_struct_decl(const StmtStructDecl& decl, const SourceSpan& span);
    TypePtr resolve_type_expr(const TypeExprPtr& te,
                              const std::unordered_map<std::string, TypePtr>& tvars,
                              const SourceSpan& fallback_span) const;
    std::set<uint32_t> env_free_vars() const;
    void unify(const TypePtr& a, const TypePtr& b, const SourceSpan& span);
    TypePtr resolve(const TypePtr& t) const;
    void constrain_int_type(const TypePtr& t, const SourceSpan& span);
    void constrain_float_type(const TypePtr& t, const SourceSpan& span);
    void default_numeric_tvars();
    [[noreturn]] void error(const std::string& msg, const SourceSpan& span) const;

    PatResult infer_pattern(const PatternPtr& pat);

    TypePtr infer(const ExprPtr& expr);
    TypePtr infer_unary(const ExprUnary& u, const SourceSpan& span);
    TypePtr infer_binary(const ExprBinary& b, const SourceSpan& span);
    TypePtr infer_match(const ExprMatch& m, const SourceSpan& span);

    TypeVarGen gen_;
    Subst subst_;
    std::set<uint32_t> int_lit_tvars_;
    std::set<uint32_t> float_lit_tvars_;
    ScopeEnv<TypeScheme> env_;
    ScopeEnv<bool> mut_env_;
    std::unordered_map<std::string, CtorInfo> ctors_;
    std::unordered_map<std::string, TypeDeclInfo> type_decls_;
    std::unordered_map<std::string, StructInfo> structs_;
    std::unordered_map<std::string, ModuleExports> modules_;
    std::vector<std::string> exported_names_;
    ModuleResolverFn module_resolver_;
    int loop_depth_ = 0;
};

}  // namespace cml
