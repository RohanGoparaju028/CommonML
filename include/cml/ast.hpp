#pragma once

#include "cml/source.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cml {

using ScopeId = uint32_t;

struct Ident {
    std::string name;
    std::vector<ScopeId> scopes;  // Sorted, deduplicated scope marks
    SourceSpan span;

    Ident flip_scope(ScopeId s) const;
    Ident add_scope(ScopeId s) const;
    bool is_subset_of(const Ident& ref) const;
};

struct TypeExpr;
struct Pattern;
struct Expr;
struct Stmt;

using TypeExprPtr = std::shared_ptr<const TypeExpr>;
using PatternPtr = std::shared_ptr<const Pattern>;
using ExprPtr = std::shared_ptr<const Expr>;
using StmtPtr = std::shared_ptr<const Stmt>;

struct TVar {
    std::string name;
};
struct TCon {
    std::string name;
    std::vector<TypeExprPtr> args;
};
struct TFun {
    TypeExprPtr param;
    TypeExprPtr ret;
};
struct TTuple {
    std::vector<TypeExprPtr> elems;
};

struct TypeExpr {
    std::variant<TVar, TCon, TFun, TTuple> node;
    SourceSpan span;
};

struct PatWildcard {};
struct PatVar {
    Ident ident;
};
struct PatInt {
    int64_t value = 0;
};
struct PatFloat {
    double value = 0.0;
};
struct PatBool {
    bool value = false;
};
struct PatString {
    std::string value;
};
struct PatUnit {};
struct PatTuple {
    std::vector<PatternPtr> elems;
};
struct PatList {
    std::vector<PatternPtr> elems;
};
struct PatCons {
    PatternPtr head;
    PatternPtr tail;
};
struct PatCtor {
    Ident ctor;
    std::vector<PatternPtr> args;
};
struct PatUnquote {
    ExprPtr expr;
};
struct PatUnquoteSlot {
    size_t index = 0;
};

struct Pattern {
    std::variant<PatWildcard, PatVar, PatInt, PatFloat, PatBool, PatString, PatUnit,
                 PatTuple, PatList, PatCons, PatCtor, PatUnquote, PatUnquoteSlot>
        node;
    SourceSpan span;
};

enum class UnaryOp {
    NegInt,
    NegFloat,
};

enum class BinaryOp {
    AddInt,
    SubInt,
    MulInt,
    DivInt,
    ModInt,
    AddFloat,
    SubFloat,
    MulFloat,
    DivFloat,
    ConcatStr,
    Cons,
    Eq,
    Neq,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
    Pipe,
    Seq,
};

struct MatchArm {
    PatternPtr pat;
    ExprPtr body;
    SourceSpan span;
};

struct ExprInt {
    int64_t value = 0;
};
struct ExprFloat {
    double value = 0.0;
};
struct ExprBool {
    bool value = false;
};
struct ExprString {
    std::string value;
};
struct ExprUnit {};
struct ExprVar {
    Ident ident;
};
struct ExprCtor {
    Ident ident;
};
struct ExprTuple {
    std::vector<ExprPtr> elems;
};
struct ExprList {
    std::vector<ExprPtr> elems;
};
struct ExprUnary {
    UnaryOp op = UnaryOp::NegInt;
    ExprPtr operand;
};
struct ExprBinary {
    BinaryOp op = BinaryOp::AddInt;
    ExprPtr left;
    ExprPtr right;
};
struct ExprIf {
    ExprPtr cond;
    ExprPtr then_branch;
    ExprPtr else_branch;
};
struct ExprLet {
    bool is_rec = false;
    PatternPtr pat;
    std::optional<TypeExprPtr> type_ann;
    ExprPtr value;
    ExprPtr body;
};
struct ExprFn {
    PatternPtr param;
    std::optional<TypeExprPtr> param_type;
    ExprPtr body;
};
struct ExprApp {
    ExprPtr fn;
    ExprPtr arg;
};
struct ExprMacroCall {
    Ident name;
    std::vector<ExprPtr> args;
};
struct ExprMatch {
    ExprPtr scrutinee;
    std::vector<MatchArm> arms;
};
struct ExprAnnot {
    ExprPtr expr;
    TypeExprPtr type;
};
struct ExprQuote {
    ExprPtr body;
};
struct ExprUnquote {
    ExprPtr expr;
};
struct ExprUnquoteSlot {
    size_t index = 0;
};

struct Expr {
    std::variant<ExprInt, ExprFloat, ExprBool, ExprString, ExprUnit, ExprVar, ExprCtor,
                 ExprTuple, ExprList, ExprUnary, ExprBinary, ExprIf, ExprLet, ExprFn,
                 ExprApp, ExprMacroCall, ExprMatch, ExprAnnot, ExprQuote, ExprUnquote,
                 ExprUnquoteSlot>
        node;
    SourceSpan span;
};

struct CtorDecl {
    Ident name;
    std::vector<TypeExprPtr> fields;
    SourceSpan span;
};

struct StmtTypeDecl {
    Ident name;
    std::vector<Ident> type_params;
    std::vector<CtorDecl> ctors;
};

struct StmtLet {
    bool is_rec = false;
    PatternPtr pat;
    std::optional<TypeExprPtr> type_ann;
    ExprPtr value;
};

struct StmtMacro {
    Ident name;
    std::vector<Ident> params;
    ExprPtr body;
};

struct StmtExpr {
    ExprPtr expr;
};

struct Stmt {
    std::variant<StmtTypeDecl, StmtLet, StmtMacro, StmtExpr> node;
    SourceSpan span;
};

struct Program {
    std::vector<StmtPtr> stmts;
};

const char* unary_op_str(UnaryOp op);
const char* binary_op_str(BinaryOp op);

std::string pattern_to_string(const PatternPtr& pat);
std::string expr_to_string(const ExprPtr& expr);
std::string type_expr_to_string(const TypeExprPtr& ty);
std::string stmt_to_string(const StmtPtr& stmt);

PatternPtr flip_scope_pattern(const PatternPtr& pat, ScopeId s);
ExprPtr flip_scope_expr(const ExprPtr& expr, ScopeId s);
PatternPtr add_scope_pattern(const PatternPtr& pat, ScopeId s);
ExprPtr add_scope_expr(const ExprPtr& expr, ScopeId s);

template <typename T>
class ScopeEnv {
public:
    struct Entry {
        Ident binder;
        T value;
    };

    void push(Ident binder, T value) {
        entries_.push_back(Entry{std::move(binder), std::move(value)});
    }

    size_t mark() const { return entries_.size(); }
    void restore(size_t mark) { entries_.resize(mark); }

    const T* lookup(const Ident& ref) const {
        const Entry* best = nullptr;
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (it->binder.is_subset_of(ref)) {
                if (best == nullptr || it->binder.scopes.size() > best->binder.scopes.size()) {
                    best = &(*it);
                }
            }
        }
        return best ? &best->value : nullptr;
    }

    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

}  // namespace cml
