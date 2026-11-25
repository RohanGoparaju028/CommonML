#include "cml/typecheck.hpp"

namespace cml {

std::vector<std::string> TypeChecker::check_program(const Program& prog) {
    std::vector<std::string> results;
    for (const auto& stmt : prog.stmts) {
        results.push_back(check_stmt(stmt));
    }
    return results;
}

std::string TypeChecker::check_stmt(const StmtPtr& stmt) {
    if (!stmt) return "";
    return std::visit(
        [&](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, StmtTypeDecl>) {
                register_type_decl(n, stmt->span);
                return "type " + n.name.name;
            } else if constexpr (std::is_same_v<T, StmtMacro>) {
                return "macro " + n.name.name;
            } else if constexpr (std::is_same_v<T, StmtLet>) {
                if (n.is_rec) {
                    auto* pv = std::get_if<PatVar>(&n.pat->node);
                    if (!pv) {
                        error("'let rec' requires a simple variable pattern", n.pat->span);
                    }
                    std::set<uint32_t> outer_fv = env_free_vars();
                    TypePtr rec_ty = gen_.fresh();
                    if (n.type_ann) {
                        std::unordered_map<std::string, TypePtr> tvars;
                        TypePtr ann_ty = resolve_type_expr(*n.type_ann, tvars, stmt->span);
                        unify(rec_ty, ann_ty, stmt->span);
                    }
                    size_t mark = env_.mark();
                    env_.push(pv->ident, TypeScheme{{}, rec_ty});
                    TypePtr val_ty = infer(n.value);
                    unify(rec_ty, val_ty, n.value->span);
                    env_.restore(mark);

                    TypePtr resolved = resolve(rec_ty);
                    TypeScheme scheme = generalize(outer_fv, resolved);
                    env_.push(pv->ident, scheme);
                    return pv->ident.name + " : " + scheme_to_string(scheme);
                } else {
                    TypePtr val_ty = infer(n.value);
                    if (n.type_ann) {
                        std::unordered_map<std::string, TypePtr> tvars;
                        TypePtr ann_ty = resolve_type_expr(*n.type_ann, tvars, stmt->span);
                        unify(val_ty, ann_ty, n.value->span);
                    }
                    PatResult pat_res = infer_pattern(n.pat);
                    unify(pat_res.type, val_ty, n.pat->span);

                    std::set<uint32_t> outer_fv = env_free_vars();
                    std::string summary;
                    for (size_t i = 0; i < pat_res.bindings.size(); ++i) {
                        const auto& [id, ty] = pat_res.bindings[i];
                        TypeScheme scheme = generalize(outer_fv, resolve(ty));
                        env_.push(id, scheme);
                        if (i > 0) summary += ", ";
                        summary += id.name + " : " + scheme_to_string(scheme);
                    }
                    if (summary.empty()) {
                        summary = "_ : " + type_to_string(resolve(val_ty));
                    }
                    return summary;
                }
            } else {
                TypePtr ty = infer(n.expr);
                return type_to_string(resolve(ty));
            }
        },
        stmt->node);
}

TypePtr TypeChecker::infer_expr(const ExprPtr& expr) {
    return resolve(infer(expr));
}

TypePtr TypeChecker::infer(const ExprPtr& expr) {
    if (!expr) return unit_type();
    return std::visit(
        [&](const auto& n) -> TypePtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ExprInt>) {
                return int_type();
            } else if constexpr (std::is_same_v<T, ExprFloat>) {
                return float_type();
            } else if constexpr (std::is_same_v<T, ExprBool>) {
                return bool_type();
            } else if constexpr (std::is_same_v<T, ExprString>) {
                return string_type();
            } else if constexpr (std::is_same_v<T, ExprUnit>) {
                return unit_type();
            } else if constexpr (std::is_same_v<T, ExprVar>) {
                const TypeScheme* scheme = env_.lookup(n.ident);
                if (!scheme) {
                    error("unbound variable '" + n.ident.name + "'", expr->span);
                }
                return instantiate(*scheme, gen_);
            } else if constexpr (std::is_same_v<T, ExprCtor>) {
                auto it = ctors_.find(n.ident.name);
                if (it == ctors_.end()) {
                    error("unknown constructor '" + n.ident.name + "'", expr->span);
                }
                return instantiate(it->second.scheme, gen_);
            } else if constexpr (std::is_same_v<T, ExprTuple>) {
                std::vector<TypePtr> elems;
                elems.reserve(n.elems.size());
                for (const auto& e : n.elems) {
                    elems.push_back(infer(e));
                }
                return make_ttuple(std::move(elems));
            } else if constexpr (std::is_same_v<T, ExprList>) {
                TypePtr elem_ty = gen_.fresh();
                for (const auto& e : n.elems) {
                    TypePtr t = infer(e);
                    unify(elem_ty, t, e->span);
                }
                return list_type(resolve(elem_ty));
            } else if constexpr (std::is_same_v<T, ExprUnary>) {
                return infer_unary(n, expr->span);
            } else if constexpr (std::is_same_v<T, ExprBinary>) {
                return infer_binary(n, expr->span);
            } else if constexpr (std::is_same_v<T, ExprIf>) {
                TypePtr cond_ty = infer(n.cond);
                unify(cond_ty, bool_type(), n.cond->span);
                TypePtr then_ty = infer(n.then_branch);
                TypePtr else_ty = infer(n.else_branch);
                unify(then_ty, else_ty, n.else_branch->span);
                return resolve(then_ty);
            } else if constexpr (std::is_same_v<T, ExprLet>) {
                size_t mark = env_.mark();
                if (n.is_rec) {
                    auto* pv = std::get_if<PatVar>(&n.pat->node);
                    if (!pv) {
                        error("'let rec' requires a simple variable pattern", n.pat->span);
                    }
                    std::set<uint32_t> outer_fv = env_free_vars();
                    TypePtr rec_ty = gen_.fresh();
                    if (n.type_ann) {
                        std::unordered_map<std::string, TypePtr> tvars;
                        TypePtr ann_ty = resolve_type_expr(*n.type_ann, tvars, expr->span);
                        unify(rec_ty, ann_ty, expr->span);
                    }
                    env_.push(pv->ident, TypeScheme{{}, rec_ty});
                    TypePtr val_ty = infer(n.value);
                    unify(rec_ty, val_ty, n.value->span);
                    env_.restore(mark);
                    TypeScheme scheme = generalize(outer_fv, resolve(rec_ty));
                    env_.push(pv->ident, scheme);
                } else {
                    TypePtr val_ty = infer(n.value);
                    if (n.type_ann) {
                        std::unordered_map<std::string, TypePtr> tvars;
                        TypePtr ann_ty = resolve_type_expr(*n.type_ann, tvars, expr->span);
                        unify(val_ty, ann_ty, n.value->span);
                    }
                    PatResult pat_res = infer_pattern(n.pat);
                    unify(pat_res.type, val_ty, n.pat->span);
                    std::set<uint32_t> outer_fv = env_free_vars();
                    for (const auto& [id, ty] : pat_res.bindings) {
                        env_.push(id, generalize(outer_fv, resolve(ty)));
                    }
                }
                TypePtr body_ty = infer(n.body);
                env_.restore(mark);
                return resolve(body_ty);
            } else if constexpr (std::is_same_v<T, ExprFn>) {
                size_t mark = env_.mark();
                PatResult pat_res = infer_pattern(n.param);
                if (n.param_type) {
                    std::unordered_map<std::string, TypePtr> tvars;
                    TypePtr ann_ty = resolve_type_expr(*n.param_type, tvars, expr->span);
                    unify(pat_res.type, ann_ty, n.param->span);
                }
                for (const auto& [id, ty] : pat_res.bindings) {
                    env_.push(id, TypeScheme{{}, resolve(ty)});
                }
                TypePtr body_ty = infer(n.body);
                env_.restore(mark);
                return make_tfun(resolve(pat_res.type), resolve(body_ty));
            } else if constexpr (std::is_same_v<T, ExprApp>) {
                TypePtr fn_ty = infer(n.fn);
                TypePtr arg_ty = infer(n.arg);
                TypePtr ret_ty = gen_.fresh();
                unify(fn_ty, make_tfun(arg_ty, ret_ty), expr->span);
                return resolve(ret_ty);
            } else if constexpr (std::is_same_v<T, ExprMacroCall>) {
                error("unexpanded macro call '" + n.name.name + "!'", expr->span);
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                return infer_match(n, expr->span);
            } else if constexpr (std::is_same_v<T, ExprAnnot>) {
                TypePtr expr_ty = infer(n.expr);
                std::unordered_map<std::string, TypePtr> tvars;
                TypePtr ann_ty = resolve_type_expr(n.type, tvars, expr->span);
                unify(expr_ty, ann_ty, expr->span);
                return resolve(expr_ty);
            } else if constexpr (std::is_same_v<T, ExprQuote>) {
                return ast_type();
            } else {
                error("unquote outside macro expansion", expr->span);
            }
        },
        expr->node);
}

TypePtr TypeChecker::infer_unary(const ExprUnary& u, const SourceSpan& span) {
    TypePtr operand_ty = infer(u.operand);
    if (u.op == UnaryOp::NegInt) {
        unify(operand_ty, int_type(), span);
        return int_type();
    }
    unify(operand_ty, float_type(), span);
    return float_type();
}

TypePtr TypeChecker::infer_binary(const ExprBinary& b, const SourceSpan& span) {
    TypePtr lt = infer(b.left);
    TypePtr rt = infer(b.right);

    switch (b.op) {
        case BinaryOp::AddInt:
        case BinaryOp::SubInt:
        case BinaryOp::MulInt:
        case BinaryOp::DivInt:
        case BinaryOp::ModInt:
            unify(lt, int_type(), b.left->span);
            unify(rt, int_type(), b.right->span);
            return int_type();

        case BinaryOp::AddFloat:
        case BinaryOp::SubFloat:
        case BinaryOp::MulFloat:
        case BinaryOp::DivFloat:
            unify(lt, float_type(), b.left->span);
            unify(rt, float_type(), b.right->span);
            return float_type();

        case BinaryOp::ConcatStr:
            unify(lt, string_type(), b.left->span);
            unify(rt, string_type(), b.right->span);
            return string_type();

        case BinaryOp::Cons: {
            TypePtr lst = list_type(lt);
            unify(rt, lst, b.right->span);
            return resolve(rt);
        }

        case BinaryOp::Eq:
        case BinaryOp::Neq:
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge:
            unify(lt, rt, span);
            return bool_type();

        case BinaryOp::And:
        case BinaryOp::Or:
            unify(lt, bool_type(), b.left->span);
            unify(rt, bool_type(), b.right->span);
            return bool_type();

        case BinaryOp::Pipe: {
            TypePtr res_ty = gen_.fresh();
            unify(rt, make_tfun(lt, res_ty), span);
            return resolve(res_ty);
        }

        case BinaryOp::Seq:
            return resolve(rt);
    }
    return unit_type();
}

TypePtr TypeChecker::infer_match(const ExprMatch& m, const SourceSpan& span) {
    TypePtr scrut_ty = infer(m.scrutinee);
    if (m.arms.empty()) {
        error("match expression must have at least one arm", span);
    }
    TypePtr result_ty = gen_.fresh();
    for (const auto& arm : m.arms) {
        size_t mark = env_.mark();
        PatResult pat_res = infer_pattern(arm.pat);
        unify(scrut_ty, pat_res.type, arm.pat->span);
        for (const auto& [id, ty] : pat_res.bindings) {
            env_.push(id, TypeScheme{{}, resolve(ty)});
        }
        TypePtr arm_ty = infer(arm.body);
        unify(result_ty, arm_ty, arm.body->span);
        env_.restore(mark);
    }
    return resolve(result_ty);
}

}  // namespace cml
