#pragma once

#include "cml/ast.hpp"
#include "cml/source.hpp"
#include "cml/type.hpp"

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

class TypeChecker {
public:
    TypeChecker();

    void register_builtins();

    // Check a full program; returns inferred types/schemes for top-level expressions/bindings.
    std::vector<std::string> check_program(const Program& prog);

    // Check a single statement and update persistent global environment.
    std::string check_stmt(const StmtPtr& stmt);

    // Infer type of a standalone expression against the current environment.
    TypePtr infer_expr(const ExprPtr& expr);

    const ScopeEnv<TypeScheme>& env() const { return env_; }
    const std::unordered_map<std::string, CtorInfo>& ctors() const { return ctors_; }
    const std::unordered_map<std::string, TypeDeclInfo>& type_decls() const { return type_decls_; }

private:
    struct PatResult {
        TypePtr type;
        std::vector<std::pair<Ident, TypePtr>> bindings;
    };

    // Environment & type expression conversion (src/types/typecheck_env.cpp)
    void register_type_decl(const StmtTypeDecl& decl, const SourceSpan& span);
    TypePtr resolve_type_expr(const TypeExprPtr& te,
                              const std::unordered_map<std::string, TypePtr>& tvars,
                              const SourceSpan& fallback_span) const;
    std::set<uint32_t> env_free_vars() const;
    void unify(const TypePtr& a, const TypePtr& b, const SourceSpan& span);
    TypePtr resolve(const TypePtr& t) const;
    [[noreturn]] void error(const std::string& msg, const SourceSpan& span) const;

    // Pattern inference (src/types/typecheck_pat.cpp)
    PatResult infer_pattern(const PatternPtr& pat);

    // Expression & statement inference (src/types/typecheck_expr.cpp)
    TypePtr infer(const ExprPtr& expr);
    TypePtr infer_unary(const ExprUnary& u, const SourceSpan& span);
    TypePtr infer_binary(const ExprBinary& b, const SourceSpan& span);
    TypePtr infer_match(const ExprMatch& m, const SourceSpan& span);

    TypeVarGen gen_;
    Subst subst_;
    ScopeEnv<TypeScheme> env_;
    std::unordered_map<std::string, CtorInfo> ctors_;
    std::unordered_map<std::string, TypeDeclInfo> type_decls_;
};

}  // namespace cml
