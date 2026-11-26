#include "cml/vm.hpp"

#include <functional>

namespace cml {

Value VM::execute_binary_op(OpCode op, const Value& a, const Value& b, const SourceSpan& span) {
    switch (op) {
        case OpCode::AddInt:
            return Value::make_int(std::get<ValInt>(a.node).value + std::get<ValInt>(b.node).value);
        case OpCode::SubInt:
            return Value::make_int(std::get<ValInt>(a.node).value - std::get<ValInt>(b.node).value);
        case OpCode::MulInt:
            return Value::make_int(std::get<ValInt>(a.node).value * std::get<ValInt>(b.node).value);
        case OpCode::DivInt: {
            int64_t denom = std::get<ValInt>(b.node).value;
            if (denom == 0) runtime_error("integer division by zero", span);
            return Value::make_int(std::get<ValInt>(a.node).value / denom);
        }
        case OpCode::ModInt: {
            int64_t denom = std::get<ValInt>(b.node).value;
            if (denom == 0) runtime_error("integer modulo by zero", span);
            return Value::make_int(std::get<ValInt>(a.node).value % denom);
        }
        case OpCode::AddFloat:
            return Value::make_float(std::get<ValFloat>(a.node).value +
                                     std::get<ValFloat>(b.node).value);
        case OpCode::SubFloat:
            return Value::make_float(std::get<ValFloat>(a.node).value -
                                     std::get<ValFloat>(b.node).value);
        case OpCode::MulFloat:
            return Value::make_float(std::get<ValFloat>(a.node).value *
                                     std::get<ValFloat>(b.node).value);
        case OpCode::DivFloat: {
            double denom = std::get<ValFloat>(b.node).value;
            if (denom == 0.0) runtime_error("float division by zero", span);
            return Value::make_float(std::get<ValFloat>(a.node).value / denom);
        }
        case OpCode::BitAnd:
            return Value::make_int(std::get<ValInt>(a.node).value & std::get<ValInt>(b.node).value);
        case OpCode::BitOr:
            return Value::make_int(std::get<ValInt>(a.node).value | std::get<ValInt>(b.node).value);
        case OpCode::BitXor:
            return Value::make_int(std::get<ValInt>(a.node).value ^ std::get<ValInt>(b.node).value);
        case OpCode::Shl: {
            int64_t sh = std::get<ValInt>(b.node).value;
            if (sh < 0 || sh >= 64) runtime_error("shift amount out of range [0, 63]", span);
            return Value::make_int(static_cast<int64_t>(
                static_cast<uint64_t>(std::get<ValInt>(a.node).value) << sh));
        }
        case OpCode::Shr: {
            int64_t sh = std::get<ValInt>(b.node).value;
            if (sh < 0 || sh >= 64) runtime_error("shift amount out of range [0, 63]", span);
            return Value::make_int(std::get<ValInt>(a.node).value >> sh);
        }
        case OpCode::ConcatStr:
            return Value::make_string(std::get<ValString>(a.node).value +
                                      std::get<ValString>(b.node).value);
        case OpCode::Cons: {
            std::vector<Value> elems;
            const auto& tail = std::get<ValList>(b.node).ptr->elems;
            elems.reserve(1 + tail.size());
            elems.push_back(a);
            elems.insert(elems.end(), tail.begin(), tail.end());
            return Value::make_list(std::move(elems));
        }
        case OpCode::Eq:
            return Value::make_bool(values_equal(a, b));
        case OpCode::Neq:
            return Value::make_bool(!values_equal(a, b));
        case OpCode::Lt:
            return Value::make_bool(compare_values(a, b) < 0);
        case OpCode::Le:
            return Value::make_bool(compare_values(a, b) <= 0);
        case OpCode::Gt:
            return Value::make_bool(compare_values(a, b) > 0);
        case OpCode::Ge:
            return Value::make_bool(compare_values(a, b) >= 0);
        default:
            runtime_error("unexpected binary opcode", span);
    }
}

static ExprPtr value_to_ast_expr(const Value& v, const SourceSpan& span) {
    return std::visit(
        [&](const auto& n) -> ExprPtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ValAstExpr>) {
                return n.expr;
            } else if constexpr (std::is_same_v<T, ValInt>) {
                return std::make_shared<Expr>(Expr{ExprInt{n.value}, span});
            } else if constexpr (std::is_same_v<T, ValFloat>) {
                return std::make_shared<Expr>(Expr{ExprFloat{n.value}, span});
            } else if constexpr (std::is_same_v<T, ValBool>) {
                return std::make_shared<Expr>(Expr{ExprBool{n.value}, span});
            } else if constexpr (std::is_same_v<T, ValString>) {
                return std::make_shared<Expr>(Expr{ExprString{n.value}, span});
            } else if constexpr (std::is_same_v<T, ValUnit>) {
                return std::make_shared<Expr>(Expr{ExprUnit{}, span});
            } else if constexpr (std::is_same_v<T, ValTuple>) {
                std::vector<ExprPtr> elems;
                for (const auto& e : n.ptr->elems) elems.push_back(value_to_ast_expr(e, span));
                return std::make_shared<Expr>(Expr{ExprTuple{std::move(elems)}, span});
            } else if constexpr (std::is_same_v<T, ValList>) {
                std::vector<ExprPtr> elems;
                for (const auto& e : n.ptr->elems) elems.push_back(value_to_ast_expr(e, span));
                return std::make_shared<Expr>(Expr{ExprList{std::move(elems)}, span});
            } else {
                throw CompileError(Diagnostic{
                    "macro", "cannot unquote value of this kind into expression", span});
            }
        },
        v.node);
}

static PatternPtr expr_to_pattern(const ExprPtr& expr, const SourceSpan& span) {
    if (!expr) return std::make_shared<Pattern>(Pattern{PatWildcard{}, span});
    return std::visit(
        [&](const auto& n) -> PatternPtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ExprVar>) {
                return std::make_shared<Pattern>(Pattern{PatVar{n.ident}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprCtor>) {
                return std::make_shared<Pattern>(Pattern{PatCtor{n.ident, {}}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprInt>) {
                return std::make_shared<Pattern>(Pattern{PatInt{n.value}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprFloat>) {
                return std::make_shared<Pattern>(Pattern{PatFloat{n.value}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprBool>) {
                return std::make_shared<Pattern>(Pattern{PatBool{n.value}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprString>) {
                return std::make_shared<Pattern>(Pattern{PatString{n.value}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprUnit>) {
                return std::make_shared<Pattern>(Pattern{PatUnit{}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprTuple>) {
                std::vector<PatternPtr> elems;
                for (const auto& e : n.elems) elems.push_back(expr_to_pattern(e, span));
                return std::make_shared<Pattern>(Pattern{PatTuple{std::move(elems)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprList>) {
                std::vector<PatternPtr> elems;
                for (const auto& e : n.elems) elems.push_back(expr_to_pattern(e, span));
                return std::make_shared<Pattern>(Pattern{PatList{std::move(elems)}, expr->span});
            } else {
                throw CompileError(Diagnostic{
                    "macro", "expression cannot be spliced as a pattern", expr->span});
            }
        },
        expr->node);
}

static PatternPtr value_to_ast_pattern(const Value& v, const SourceSpan& span) {
    if (auto* p = std::get_if<ValAstPattern>(&v.node)) {
        return p->pat;
    }
    if (auto* e = std::get_if<ValAstExpr>(&v.node)) {
        return expr_to_pattern(e->expr, span);
    }
    return expr_to_pattern(value_to_ast_expr(v, span), span);
}

Value VM::instantiate_quote(const QuoteTemplate& qt, const std::vector<Value>& slots,
                            const SourceSpan& span) {
    std::function<PatternPtr(const PatternPtr&)> subst_pat;
    std::function<ExprPtr(const ExprPtr&)> subst_expr;

    subst_pat = [&](const PatternPtr& pat) -> PatternPtr {
        if (!pat) return nullptr;
        return std::visit(
            [&](const auto& n) -> PatternPtr {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, PatUnquoteSlot>) {
                    return value_to_ast_pattern(slots.at(n.index), pat->span);
                } else if constexpr (std::is_same_v<T, PatTuple>) {
                    std::vector<PatternPtr> elems;
                    for (const auto& e : n.elems) elems.push_back(subst_pat(e));
                    return std::make_shared<Pattern>(Pattern{PatTuple{std::move(elems)}, pat->span});
                } else if constexpr (std::is_same_v<T, PatList>) {
                    std::vector<PatternPtr> elems;
                    for (const auto& e : n.elems) elems.push_back(subst_pat(e));
                    return std::make_shared<Pattern>(Pattern{PatList{std::move(elems)}, pat->span});
                } else if constexpr (std::is_same_v<T, PatCons>) {
                    return std::make_shared<Pattern>(
                        Pattern{PatCons{subst_pat(n.head), subst_pat(n.tail)}, pat->span});
                } else if constexpr (std::is_same_v<T, PatCtor>) {
                    std::vector<PatternPtr> args;
                    for (const auto& a : n.args) args.push_back(subst_pat(a));
                    return std::make_shared<Pattern>(
                        Pattern{PatCtor{n.ctor, std::move(args)}, pat->span});
                } else {
                    return pat;
                }
            },
            pat->node);
    };

    subst_expr = [&](const ExprPtr& expr) -> ExprPtr {
        if (!expr) return nullptr;
        return std::visit(
            [&](const auto& n) -> ExprPtr {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, ExprUnquoteSlot>) {
                    return value_to_ast_expr(slots.at(n.index), expr->span);
                } else if constexpr (std::is_same_v<T, ExprTuple>) {
                    std::vector<ExprPtr> elems;
                    for (const auto& e : n.elems) elems.push_back(subst_expr(e));
                    return std::make_shared<Expr>(Expr{ExprTuple{std::move(elems)}, expr->span});
                } else if constexpr (std::is_same_v<T, ExprList>) {
                    std::vector<ExprPtr> elems;
                    for (const auto& e : n.elems) elems.push_back(subst_expr(e));
                    return std::make_shared<Expr>(Expr{ExprList{std::move(elems)}, expr->span});
                } else if constexpr (std::is_same_v<T, ExprUnary>) {
                    return std::make_shared<Expr>(
                        Expr{ExprUnary{n.op, subst_expr(n.operand)}, expr->span});
                } else if constexpr (std::is_same_v<T, ExprBinary>) {
                    return std::make_shared<Expr>(
                        Expr{ExprBinary{n.op, subst_expr(n.left), subst_expr(n.right)},
                             expr->span});
                } else if constexpr (std::is_same_v<T, ExprIf>) {
                    return std::make_shared<Expr>(
                        Expr{ExprIf{subst_expr(n.cond), subst_expr(n.then_branch),
                                    subst_expr(n.else_branch)},
                             expr->span});
                } else if constexpr (std::is_same_v<T, ExprLet>) {
                    return std::make_shared<Expr>(
                        Expr{ExprLet{n.is_rec, subst_pat(n.pat), n.type_ann, subst_expr(n.value),
                                     subst_expr(n.body)},
                             expr->span});
                } else if constexpr (std::is_same_v<T, ExprFn>) {
                    return std::make_shared<Expr>(
                        Expr{ExprFn{subst_pat(n.param), n.param_type, subst_expr(n.body)},
                             expr->span});
                } else if constexpr (std::is_same_v<T, ExprApp>) {
                    return std::make_shared<Expr>(
                        Expr{ExprApp{subst_expr(n.fn), subst_expr(n.arg)}, expr->span});
                } else if constexpr (std::is_same_v<T, ExprMacroCall>) {
                    std::vector<ExprPtr> args;
                    for (const auto& a : n.args) args.push_back(subst_expr(a));
                    return std::make_shared<Expr>(
                        Expr{ExprMacroCall{n.name, std::move(args)}, expr->span});
                } else if constexpr (std::is_same_v<T, ExprMatch>) {
                    std::vector<MatchArm> arms;
                    for (const auto& arm : n.arms) {
                        arms.push_back(
                            MatchArm{subst_pat(arm.pat), subst_expr(arm.body), arm.span});
                    }
                    return std::make_shared<Expr>(
                        Expr{ExprMatch{subst_expr(n.scrutinee), std::move(arms)}, expr->span});
                } else if constexpr (std::is_same_v<T, ExprAnnot>) {
                    return std::make_shared<Expr>(
                        Expr{ExprAnnot{subst_expr(n.expr), n.type}, expr->span});
                } else if constexpr (std::is_same_v<T, ExprQuote>) {
                    return std::make_shared<Expr>(
                        Expr{ExprQuote{subst_expr(n.body)}, expr->span});
                } else {
                    return expr;
                }
            },
            expr->node);
    };

    (void)span;
    return Value::make_ast_expr(subst_expr(qt.expr));
}

}  // namespace cml
