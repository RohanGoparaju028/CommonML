#include "cml/ast.hpp"

#include <algorithm>
#include <sstream>

namespace cml {

Ident Ident::flip_scope(ScopeId s) const {
    Ident out = *this;
    auto it = std::lower_bound(out.scopes.begin(), out.scopes.end(), s);
    if (it != out.scopes.end() && *it == s) {
        out.scopes.erase(it);
    } else {
        out.scopes.insert(it, s);
    }
    return out;
}

Ident Ident::add_scope(ScopeId s) const {
    Ident out = *this;
    auto it = std::lower_bound(out.scopes.begin(), out.scopes.end(), s);
    if (it == out.scopes.end() || *it != s) {
        out.scopes.insert(it, s);
    }
    return out;
}

bool Ident::is_subset_of(const Ident& ref) const {
    if (name != ref.name) {
        return false;
    }
    return std::includes(ref.scopes.begin(), ref.scopes.end(),
                         scopes.begin(), scopes.end());
}

const char* unary_op_str(UnaryOp op) {
    switch (op) {
        case UnaryOp::NegInt: return "-";
        case UnaryOp::NegFloat: return "-.";
    }
    return "-";
}

const char* binary_op_str(BinaryOp op) {
    switch (op) {
        case BinaryOp::AddInt: return "+";
        case BinaryOp::SubInt: return "-";
        case BinaryOp::MulInt: return "*";
        case BinaryOp::DivInt: return "/";
        case BinaryOp::ModInt: return "%";
        case BinaryOp::AddFloat: return "+.";
        case BinaryOp::SubFloat: return "-.";
        case BinaryOp::MulFloat: return "*.";
        case BinaryOp::DivFloat: return "/.";
        case BinaryOp::ConcatStr: return "++";
        case BinaryOp::Cons: return "::";
        case BinaryOp::Eq: return "==";
        case BinaryOp::Neq: return "!=";
        case BinaryOp::Lt: return "<";
        case BinaryOp::Le: return "<=";
        case BinaryOp::Gt: return ">";
        case BinaryOp::Ge: return ">=";
        case BinaryOp::And: return "&&";
        case BinaryOp::Or: return "||";
        case BinaryOp::Pipe: return "|>";
        case BinaryOp::Seq: return ";";
    }
    return "?";
}

static std::string escape_str(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\0': out += "\\0"; break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
    return out;
}

std::string type_expr_to_string(const TypeExprPtr& ty) {
    if (!ty) return "?";
    return std::visit(
        [](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, TVar>) {
                return n.name;
            } else if constexpr (std::is_same_v<T, TCon>) {
                if (n.args.empty()) return n.name;
                std::string s = n.name;
                for (const auto& a : n.args) {
                    s += " (" + type_expr_to_string(a) + ")";
                }
                return s;
            } else if constexpr (std::is_same_v<T, TFun>) {
                return "(" + type_expr_to_string(n.param) + " -> " +
                       type_expr_to_string(n.ret) + ")";
            } else {
                std::string s = "(";
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    if (i > 0) s += " * ";
                    s += type_expr_to_string(n.elems[i]);
                }
                s += ")";
                return s;
            }
        },
        ty->node);
}

std::string pattern_to_string(const PatternPtr& pat) {
    if (!pat) return "_";
    return std::visit(
        [](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, PatWildcard>) {
                return "_";
            } else if constexpr (std::is_same_v<T, PatVar>) {
                return n.ident.name;
            } else if constexpr (std::is_same_v<T, PatInt>) {
                return std::to_string(n.value);
            } else if constexpr (std::is_same_v<T, PatFloat>) {
                return std::to_string(n.value);
            } else if constexpr (std::is_same_v<T, PatBool>) {
                return n.value ? "true" : "false";
            } else if constexpr (std::is_same_v<T, PatString>) {
                return escape_str(n.value);
            } else if constexpr (std::is_same_v<T, PatUnit>) {
                return "()";
            } else if constexpr (std::is_same_v<T, PatTuple>) {
                std::string s = "(";
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += pattern_to_string(n.elems[i]);
                }
                s += ")";
                return s;
            } else if constexpr (std::is_same_v<T, PatList>) {
                std::string s = "[";
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += pattern_to_string(n.elems[i]);
                }
                s += "]";
                return s;
            } else if constexpr (std::is_same_v<T, PatCons>) {
                return "(" + pattern_to_string(n.head) + " :: " +
                       pattern_to_string(n.tail) + ")";
            } else if constexpr (std::is_same_v<T, PatCtor>) {
                if (n.args.empty()) return n.ctor.name;
                std::string s = "(" + n.ctor.name;
                for (const auto& a : n.args) {
                    s += " " + pattern_to_string(a);
                }
                s += ")";
                return s;
            } else if constexpr (std::is_same_v<T, PatUnquote>) {
                return "$(" + expr_to_string(n.expr) + ")";
            } else {
                return "$slot" + std::to_string(n.index);
            }
        },
        pat->node);
}

std::string expr_to_string(const ExprPtr& expr) {
    if (!expr) return "<null>";
    return std::visit(
        [](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ExprInt>) {
                return std::to_string(n.value);
            } else if constexpr (std::is_same_v<T, ExprFloat>) {
                std::ostringstream os;
                os << n.value;
                return os.str();
            } else if constexpr (std::is_same_v<T, ExprBool>) {
                return n.value ? "true" : "false";
            } else if constexpr (std::is_same_v<T, ExprString>) {
                return escape_str(n.value);
            } else if constexpr (std::is_same_v<T, ExprUnit>) {
                return "()";
            } else if constexpr (std::is_same_v<T, ExprVar>) {
                return n.ident.name;
            } else if constexpr (std::is_same_v<T, ExprCtor>) {
                return n.ident.name;
            } else if constexpr (std::is_same_v<T, ExprTuple>) {
                std::string s = "(";
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += expr_to_string(n.elems[i]);
                }
                s += ")";
                return s;
            } else if constexpr (std::is_same_v<T, ExprList>) {
                std::string s = "[";
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += expr_to_string(n.elems[i]);
                }
                s += "]";
                return s;
            } else if constexpr (std::is_same_v<T, ExprUnary>) {
                return std::string("(") + unary_op_str(n.op) + expr_to_string(n.operand) + ")";
            } else if constexpr (std::is_same_v<T, ExprBinary>) {
                return "(" + expr_to_string(n.left) + " " + binary_op_str(n.op) + " " +
                       expr_to_string(n.right) + ")";
            } else if constexpr (std::is_same_v<T, ExprIf>) {
                return "(if " + expr_to_string(n.cond) + " then " +
                       expr_to_string(n.then_branch) + " else " +
                       expr_to_string(n.else_branch) + ")";
            } else if constexpr (std::is_same_v<T, ExprLet>) {
                std::string s = n.is_rec ? "(let rec " : "(let ";
                s += pattern_to_string(n.pat);
                if (n.type_ann) {
                    s += " : " + type_expr_to_string(*n.type_ann);
                }
                s += " = " + expr_to_string(n.value) + " in " + expr_to_string(n.body) + ")";
                return s;
            } else if constexpr (std::is_same_v<T, ExprFn>) {
                std::string s = "(fn " + pattern_to_string(n.param);
                if (n.param_type) {
                    s += " : " + type_expr_to_string(*n.param_type);
                }
                s += " -> " + expr_to_string(n.body) + ")";
                return s;
            } else if constexpr (std::is_same_v<T, ExprApp>) {
                return "(" + expr_to_string(n.fn) + " " + expr_to_string(n.arg) + ")";
            } else if constexpr (std::is_same_v<T, ExprMacroCall>) {
                std::string s = n.name.name + "!(";
                for (size_t i = 0; i < n.args.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += expr_to_string(n.args[i]);
                }
                s += ")";
                return s;
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                std::string s = "(match " + expr_to_string(n.scrutinee) + " with";
                for (const auto& arm : n.arms) {
                    s += " | " + pattern_to_string(arm.pat) + " -> " + expr_to_string(arm.body);
                }
                s += ")";
                return s;
            } else if constexpr (std::is_same_v<T, ExprAnnot>) {
                return "(" + expr_to_string(n.expr) + " : " +
                       type_expr_to_string(n.type) + ")";
            } else if constexpr (std::is_same_v<T, ExprQuote>) {
                return "quote { " + expr_to_string(n.body) + " }";
            } else if constexpr (std::is_same_v<T, ExprUnquote>) {
                return "$(" + expr_to_string(n.expr) + ")";
            } else {
                return "$slot" + std::to_string(n.index);
            }
        },
        expr->node);
}

std::string stmt_to_string(const StmtPtr& stmt) {
    if (!stmt) return "";
    return std::visit(
        [](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, StmtTypeDecl>) {
                std::string s = "type " + n.name.name;
                for (const auto& tp : n.type_params) s += " " + tp.name;
                s += " =";
                for (size_t i = 0; i < n.ctors.size(); ++i) {
                    s += (i == 0 ? " " : " | ") + n.ctors[i].name.name;
                    for (const auto& f : n.ctors[i].fields) {
                        s += " " + type_expr_to_string(f);
                    }
                }
                return s;
            } else if constexpr (std::is_same_v<T, StmtLet>) {
                std::string s = n.is_rec ? "let rec " : "let ";
                s += pattern_to_string(n.pat);
                if (n.type_ann) s += " : " + type_expr_to_string(*n.type_ann);
                s += " = " + expr_to_string(n.value);
                return s;
            } else if constexpr (std::is_same_v<T, StmtMacro>) {
                std::string s = "macro " + n.name.name;
                for (const auto& p : n.params) s += " " + p.name;
                s += " = " + expr_to_string(n.body);
                return s;
            } else {
                return expr_to_string(n.expr);
            }
        },
        stmt->node);
}

template <typename Fn>
static PatternPtr map_scopes_pat(const PatternPtr& pat, Fn&& ident_fn, bool enter_unquote) {
    if (!pat) return nullptr;
    return std::visit(
        [&](const auto& n) -> PatternPtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, PatWildcard> || std::is_same_v<T, PatInt> ||
                          std::is_same_v<T, PatFloat> || std::is_same_v<T, PatBool> ||
                          std::is_same_v<T, PatString> || std::is_same_v<T, PatUnit> ||
                          std::is_same_v<T, PatUnquoteSlot>) {
                return pat;
            } else if constexpr (std::is_same_v<T, PatVar>) {
                return std::make_shared<Pattern>(Pattern{PatVar{ident_fn(n.ident)}, pat->span});
            } else if constexpr (std::is_same_v<T, PatTuple>) {
                std::vector<PatternPtr> elems;
                elems.reserve(n.elems.size());
                for (const auto& e : n.elems)
                    elems.push_back(map_scopes_pat(e, ident_fn, enter_unquote));
                return std::make_shared<Pattern>(Pattern{PatTuple{std::move(elems)}, pat->span});
            } else if constexpr (std::is_same_v<T, PatList>) {
                std::vector<PatternPtr> elems;
                elems.reserve(n.elems.size());
                for (const auto& e : n.elems)
                    elems.push_back(map_scopes_pat(e, ident_fn, enter_unquote));
                return std::make_shared<Pattern>(Pattern{PatList{std::move(elems)}, pat->span});
            } else if constexpr (std::is_same_v<T, PatCons>) {
                return std::make_shared<Pattern>(
                    Pattern{PatCons{map_scopes_pat(n.head, ident_fn, enter_unquote),
                                    map_scopes_pat(n.tail, ident_fn, enter_unquote)},
                            pat->span});
            } else if constexpr (std::is_same_v<T, PatCtor>) {
                std::vector<PatternPtr> args;
                args.reserve(n.args.size());
                for (const auto& a : n.args)
                    args.push_back(map_scopes_pat(a, ident_fn, enter_unquote));
                return std::make_shared<Pattern>(
                    Pattern{PatCtor{ident_fn(n.ctor), std::move(args)}, pat->span});
            } else {
                return pat;
            }
        },
        pat->node);
}

template <typename Fn>
static ExprPtr map_scopes_expr(const ExprPtr& expr, Fn&& ident_fn, bool enter_unquote) {
    if (!expr) return nullptr;
    return std::visit(
        [&](const auto& n) -> ExprPtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ExprInt> || std::is_same_v<T, ExprFloat> ||
                          std::is_same_v<T, ExprBool> || std::is_same_v<T, ExprString> ||
                          std::is_same_v<T, ExprUnit> || std::is_same_v<T, ExprUnquoteSlot>) {
                return expr;
            } else if constexpr (std::is_same_v<T, ExprVar>) {
                return std::make_shared<Expr>(Expr{ExprVar{ident_fn(n.ident)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprCtor>) {
                return std::make_shared<Expr>(Expr{ExprCtor{ident_fn(n.ident)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprTuple>) {
                std::vector<ExprPtr> elems;
                elems.reserve(n.elems.size());
                for (const auto& e : n.elems)
                    elems.push_back(map_scopes_expr(e, ident_fn, enter_unquote));
                return std::make_shared<Expr>(Expr{ExprTuple{std::move(elems)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprList>) {
                std::vector<ExprPtr> elems;
                elems.reserve(n.elems.size());
                for (const auto& e : n.elems)
                    elems.push_back(map_scopes_expr(e, ident_fn, enter_unquote));
                return std::make_shared<Expr>(Expr{ExprList{std::move(elems)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprUnary>) {
                return std::make_shared<Expr>(
                    Expr{ExprUnary{n.op, map_scopes_expr(n.operand, ident_fn, enter_unquote)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprBinary>) {
                return std::make_shared<Expr>(
                    Expr{ExprBinary{n.op, map_scopes_expr(n.left, ident_fn, enter_unquote),
                                    map_scopes_expr(n.right, ident_fn, enter_unquote)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprIf>) {
                return std::make_shared<Expr>(
                    Expr{ExprIf{map_scopes_expr(n.cond, ident_fn, enter_unquote),
                                map_scopes_expr(n.then_branch, ident_fn, enter_unquote),
                                map_scopes_expr(n.else_branch, ident_fn, enter_unquote)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprLet>) {
                return std::make_shared<Expr>(
                    Expr{ExprLet{n.is_rec, map_scopes_pat(n.pat, ident_fn, enter_unquote),
                                 n.type_ann, map_scopes_expr(n.value, ident_fn, enter_unquote),
                                 map_scopes_expr(n.body, ident_fn, enter_unquote)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprFn>) {
                return std::make_shared<Expr>(
                    Expr{ExprFn{map_scopes_pat(n.param, ident_fn, enter_unquote), n.param_type,
                                map_scopes_expr(n.body, ident_fn, enter_unquote)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprApp>) {
                return std::make_shared<Expr>(
                    Expr{ExprApp{map_scopes_expr(n.fn, ident_fn, enter_unquote),
                                 map_scopes_expr(n.arg, ident_fn, enter_unquote)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprMacroCall>) {
                std::vector<ExprPtr> args;
                args.reserve(n.args.size());
                for (const auto& a : n.args)
                    args.push_back(map_scopes_expr(a, ident_fn, enter_unquote));
                return std::make_shared<Expr>(
                    Expr{ExprMacroCall{ident_fn(n.name), std::move(args)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                std::vector<MatchArm> arms;
                arms.reserve(n.arms.size());
                for (const auto& arm : n.arms) {
                    arms.push_back(
                        MatchArm{map_scopes_pat(arm.pat, ident_fn, enter_unquote),
                                 map_scopes_expr(arm.body, ident_fn, enter_unquote), arm.span});
                }
                return std::make_shared<Expr>(
                    Expr{ExprMatch{map_scopes_expr(n.scrutinee, ident_fn, enter_unquote),
                                   std::move(arms)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprAnnot>) {
                return std::make_shared<Expr>(
                    Expr{ExprAnnot{map_scopes_expr(n.expr, ident_fn, enter_unquote), n.type},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprQuote>) {
                return std::make_shared<Expr>(
                    Expr{ExprQuote{map_scopes_expr(n.body, ident_fn, enter_unquote)}, expr->span});
            } else {
                // ExprUnquote
                if (!enter_unquote) return expr;
                return std::make_shared<Expr>(
                    Expr{ExprUnquote{map_scopes_expr(n.expr, ident_fn, enter_unquote)},
                         expr->span});
            }
        },
        expr->node);
}

PatternPtr flip_scope_pattern(const PatternPtr& pat, ScopeId s) {
    return map_scopes_pat(
        pat, [s](const Ident& id) { return id.flip_scope(s); }, true);
}

ExprPtr flip_scope_expr(const ExprPtr& expr, ScopeId s) {
    return map_scopes_expr(
        expr, [s](const Ident& id) { return id.flip_scope(s); }, true);
}

PatternPtr add_scope_pattern(const PatternPtr& pat, ScopeId s) {
    return map_scopes_pat(
        pat, [s](const Ident& id) { return id.add_scope(s); }, false);
}

ExprPtr add_scope_expr(const ExprPtr& expr, ScopeId s) {
    return map_scopes_expr(
        expr, [s](const Ident& id) { return id.add_scope(s); }, false);
}

}  // namespace cml
