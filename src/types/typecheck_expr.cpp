#include "cml/typecheck.hpp"

#include <unordered_set>

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
                for (const auto& c : n.ctors) {
                    exported_names_.push_back(c.name.name);
                }
                return "type " + n.name.name;
            } else if constexpr (std::is_same_v<T, StmtStructDecl>) {
                register_struct_decl(n, stmt->span);
                return "struct " + n.name.name;
            } else if constexpr (std::is_same_v<T, StmtMacro>) {
                return "macro " + n.name.name;
            } else if constexpr (std::is_same_v<T, StmtImport>) {
                return "import " + n.path;
            } else if constexpr (std::is_same_v<T, StmtVar>) {
                TypePtr val_ty = infer(n.value);
                if (n.type_ann) {
                    std::unordered_map<std::string, TypePtr> tvars;
                    TypePtr ann_ty = resolve_type_expr(*n.type_ann, tvars, stmt->span);
                    unify(val_ty, ann_ty, n.value->span);
                }
                default_numeric_tvars();
                TypePtr res = resolve(val_ty);
                env_.push(n.name, TypeScheme{{}, res});
                mut_env_.push(n.name, true);
                exported_names_.push_back(n.name.name);
                return "var " + n.name.name + " : " + type_to_string(res);
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
                    size_t mut_mark = mut_env_.mark();
                    env_.push(pv->ident, TypeScheme{{}, rec_ty});
                    mut_env_.push(pv->ident, false);
                    TypePtr val_ty = infer(n.value);
                    unify(rec_ty, val_ty, n.value->span);
                    env_.restore(mark);
                    mut_env_.restore(mut_mark);
                    default_numeric_tvars();

                    TypePtr resolved = resolve(rec_ty);
                    TypeScheme scheme = generalize(outer_fv, resolved);
                    env_.push(pv->ident, scheme);
                    mut_env_.push(pv->ident, false);
                    exported_names_.push_back(pv->ident.name);
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
                    default_numeric_tvars();

                    std::set<uint32_t> outer_fv = env_free_vars();
                    std::string summary;
                    for (size_t i = 0; i < pat_res.bindings.size(); ++i) {
                        const auto& [id, ty] = pat_res.bindings[i];
                        TypeScheme scheme = generalize(outer_fv, resolve(ty));
                        env_.push(id, scheme);
                        mut_env_.push(id, false);
                        exported_names_.push_back(id.name);
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
                default_numeric_tvars();
                return type_to_string(resolve(ty));
            }
        },
        stmt->node);
}

TypePtr TypeChecker::infer_expr(const ExprPtr& expr) {
    TypePtr t = infer(expr);
    default_numeric_tvars();
    return resolve(t);
}

void TypeChecker::constrain_int_type(const TypePtr& t, const SourceSpan& span) {
    TypePtr r = resolve(t);
    if (auto* tv = std::get_if<TypeVar>(&r->node)) {
        int_lit_tvars_.insert(tv->id);
        return;
    }
    if (auto* tc = std::get_if<TypeCon>(&r->node)) {
        if (is_int_type_name(tc->name) && tc->args.empty()) {
            return;
        }
    }
    error("expected integer type, got '" + type_to_string(r) + "'", span);
}

void TypeChecker::constrain_float_type(const TypePtr& t, const SourceSpan& span) {
    TypePtr r = resolve(t);
    if (auto* tv = std::get_if<TypeVar>(&r->node)) {
        float_lit_tvars_.insert(tv->id);
        return;
    }
    if (auto* tc = std::get_if<TypeCon>(&r->node)) {
        if (is_float_type_name(tc->name) && tc->args.empty()) {
            return;
        }
    }
    error("expected float type, got '" + type_to_string(r) + "'", span);
}

void TypeChecker::default_numeric_tvars() {
    std::set<uint32_t> outer_fv = env_free_vars();
    for (auto it = int_lit_tvars_.begin(); it != int_lit_tvars_.end();) {
        TypePtr r = resolve(make_tvar(*it));
        if (auto* tv = std::get_if<TypeVar>(&r->node)) {
            if (!outer_fv.count(tv->id)) {
                subst_[tv->id] = int_type();
                it = int_lit_tvars_.erase(it);
                continue;
            }
        } else {
            it = int_lit_tvars_.erase(it);
            continue;
        }
        ++it;
    }
    for (auto it = float_lit_tvars_.begin(); it != float_lit_tvars_.end();) {
        TypePtr r = resolve(make_tvar(*it));
        if (auto* tv = std::get_if<TypeVar>(&r->node)) {
            if (!outer_fv.count(tv->id)) {
                subst_[tv->id] = float_type();
                it = float_lit_tvars_.erase(it);
                continue;
            }
        } else {
            it = float_lit_tvars_.erase(it);
            continue;
        }
        ++it;
    }
}

TypePtr TypeChecker::infer(const ExprPtr& expr) {
    if (!expr) return unit_type();
    return std::visit(
        [&](const auto& n) -> TypePtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ExprInt>) {
                TypePtr tv = gen_.fresh();
                int_lit_tvars_.insert(std::get<TypeVar>(tv->node).id);
                return tv;
            } else if constexpr (std::is_same_v<T, ExprFloat>) {
                TypePtr tv = gen_.fresh();
                float_lit_tvars_.insert(std::get<TypeVar>(tv->node).id);
                return tv;
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
            } else if constexpr (std::is_same_v<T, ExprImport>) {
                if (!module_resolver_) {
                    error("import resolver not configured", expr->span);
                }
                ModuleExports exp = module_resolver_(n.path, expr->span);
                std::string mod_key = exp.module_id;
                for (const auto& [sn, si] : exp.structs) {
                    if (!structs_.count(sn)) {
                        register_struct_info(si);
                    }
                }
                for (const auto& [tn, ti] : exp.type_decls) {
                    if (!type_decls_.count(tn)) {
                        type_decls_[tn] = ti;
                    }
                }
                for (const auto& [cn, ci] : exp.ctors) {
                    if (!ctors_.count(cn)) {
                        ctors_[cn] = ci;
                    }
                }
                modules_[mod_key] = std::move(exp);
                return make_tcon("$Mod:" + mod_key);
            } else if constexpr (std::is_same_v<T, ExprStructLit>) {
                auto it = structs_.find(n.type_name.name);
                if (it == structs_.end()) {
                    error("unknown struct '" + n.type_name.name + "'", expr->span);
                }
                const StructInfo& info = it->second;
                Subst param_subst;
                std::vector<TypePtr> args;
                args.reserve(info.type_params.size());
                for (uint32_t tp : info.type_params) {
                    TypePtr tv = gen_.fresh();
                    param_subst[tp] = tv;
                    args.push_back(tv);
                }

                std::unordered_map<std::string, TypePtr> expected_fields;
                for (const auto& [fname, fty] : info.fields) {
                    expected_fields[fname] = apply_subst(param_subst, fty);
                }

                std::unordered_set<std::string> provided;
                for (const auto& [fname_id, fval] : n.fields) {
                    auto fit = expected_fields.find(fname_id.name);
                    if (fit == expected_fields.end()) {
                        error("unknown field '" + fname_id.name + "' in struct '" + info.name + "'",
                              fname_id.span);
                    }
                    if (!provided.insert(fname_id.name).second) {
                        error("duplicate field '" + fname_id.name + "' in struct '" + info.name + "'",
                              fname_id.span);
                    }
                    TypePtr vt = infer(fval);
                    unify(vt, fit->second, fval->span);
                }
                for (const auto& [fname, _] : info.fields) {
                    if (!provided.count(fname)) {
                        error("missing field '" + fname + "' in struct '" + info.name + "'",
                              expr->span);
                    }
                }
                std::vector<TypePtr> resolved_args;
                for (const auto& a : args) resolved_args.push_back(resolve(a));
                return make_tcon(info.name, std::move(resolved_args));
            } else if constexpr (std::is_same_v<T, ExprFieldGet>) {
                TypePtr obj_ty = resolve(infer(n.obj));
                auto* tc = std::get_if<TypeCon>(&obj_ty->node);
                if (!tc) {
                    error("cannot access field '" + n.field.name + "' on non-struct type '" +
                              type_to_string(obj_ty) + "'",
                          expr->span);
                }
                if (tc->name.rfind("$Mod:", 0) == 0) {
                    std::string mod_id = tc->name.substr(5);
                    auto mit = modules_.find(mod_id);
                    if (mit == modules_.end()) {
                        error("unknown module '" + mod_id + "'", expr->span);
                    }
                    auto vit = mit->second.values.find(n.field.name);
                    if (vit != mit->second.values.end()) {
                        return instantiate(vit->second, gen_);
                    }
                    auto cit = mit->second.ctors.find(n.field.name);
                    if (cit != mit->second.ctors.end()) {
                        return instantiate(cit->second.scheme, gen_);
                    }
                    error("module has no member '" + n.field.name + "'", n.field.span);
                }
                auto sit = structs_.find(tc->name);
                if (sit == structs_.end()) {
                    if (tc->name == "Ptr") {
                        error("cannot access field '" + n.field.name + "' with '.' on pointer type '" +
                                  type_to_string(obj_ty) + "' (use '->')",
                              expr->span);
                    }
                    error("type '" + tc->name + "' has no fields", expr->span);
                }
                const StructInfo& info = sit->second;
                Subst s;
                for (size_t i = 0; i < info.type_params.size() && i < tc->args.size(); ++i) {
                    s[info.type_params[i]] = tc->args[i];
                }
                for (const auto& [fname, fty] : info.fields) {
                    if (fname == n.field.name) {
                        return resolve(apply_subst(s, fty));
                    }
                }
                error("struct '" + info.name + "' has no field '" + n.field.name + "'",
                      n.field.span);
            } else if constexpr (std::is_same_v<T, ExprFieldSet>) {
                TypePtr obj_ty = resolve(infer(n.obj));
                auto* tc = std::get_if<TypeCon>(&obj_ty->node);
                if (!tc || tc->name.rfind("$Mod:", 0) == 0) {
                    error("cannot assign field '" + n.field.name + "' on type '" +
                              type_to_string(obj_ty) + "'",
                          expr->span);
                }
                auto sit = structs_.find(tc->name);
                if (sit == structs_.end()) {
                    error("type '" + tc->name + "' is not a struct", expr->span);
                }
                const StructInfo& info = sit->second;
                Subst s;
                for (size_t i = 0; i < info.type_params.size() && i < tc->args.size(); ++i) {
                    s[info.type_params[i]] = tc->args[i];
                }
                TypePtr field_ty;
                for (const auto& [fname, fty] : info.fields) {
                    if (fname == n.field.name) {
                        field_ty = apply_subst(s, fty);
                        break;
                    }
                }
                if (!field_ty) {
                    error("struct '" + info.name + "' has no field '" + n.field.name + "'",
                          n.field.span);
                }
                TypePtr val_ty = infer(n.value);
                unify(val_ty, field_ty, n.value->span);
                return unit_type();
            } else if constexpr (std::is_same_v<T, ExprNew>) {
                TypePtr vt = infer(n.value);
                return ptr_type(resolve(vt));
            } else if constexpr (std::is_same_v<T, ExprClose>) {
                TypePtr pt = infer(n.ptr);
                TypePtr inner = gen_.fresh();
                unify(pt, ptr_type(inner), n.ptr->span);
                return unit_type();
            } else if constexpr (std::is_same_v<T, ExprDefer>) {
                (void)infer(n.action);
                return unit_type();
            } else if constexpr (std::is_same_v<T, ExprDerefSet>) {
                TypePtr pt = infer(n.ptr);
                TypePtr vt = infer(n.value);
                unify(pt, ptr_type(vt), expr->span);
                return unit_type();
            } else if constexpr (std::is_same_v<T, ExprPtrFieldGet> ||
                                 std::is_same_v<T, ExprPtrFieldSet>) {
                TypePtr ptr_ty = resolve(infer(n.ptr));
                TypePtr inner = gen_.fresh();
                unify(ptr_ty, ptr_type(inner), n.ptr->span);
                TypePtr struct_ty = resolve(inner);
                auto* tc = std::get_if<TypeCon>(&struct_ty->node);
                if (!tc) {
                    error("cannot access field '" + n.field.name + "' with '->' on non-struct pointer '" +
                              type_to_string(ptr_ty) + "'",
                          expr->span);
                }
                auto sit = structs_.find(tc->name);
                if (sit == structs_.end()) {
                    error("type '" + tc->name + "' is not a struct", expr->span);
                }
                const StructInfo& info = sit->second;
                Subst s;
                for (size_t i = 0; i < info.type_params.size() && i < tc->args.size(); ++i) {
                    s[info.type_params[i]] = tc->args[i];
                }
                TypePtr field_ty;
                for (const auto& [fname, fty] : info.fields) {
                    if (fname == n.field.name) {
                        field_ty = resolve(apply_subst(s, fty));
                        break;
                    }
                }
                if (!field_ty) {
                    error("struct '" + info.name + "' has no field '" + n.field.name + "'",
                          n.field.span);
                }
                if constexpr (std::is_same_v<T, ExprPtrFieldSet>) {
                    TypePtr val_ty = infer(n.value);
                    unify(val_ty, field_ty, n.value->span);
                    return unit_type();
                } else {
                    return field_ty;
                }
            } else if constexpr (std::is_same_v<T, ExprAssign>) {
                const TypeScheme* scheme = env_.lookup(n.target);
                if (!scheme) {
                    error("unbound variable '" + n.target.name + "'", expr->span);
                }
                const bool* is_mut = mut_env_.lookup(n.target);
                if (!is_mut || !*is_mut) {
                    error("cannot assign to immutable binding '" + n.target.name +
                              "' (use 'var' for mutable variables)",
                          expr->span);
                }
                TypePtr target_ty = instantiate(*scheme, gen_);
                TypePtr val_ty = infer(n.value);
                unify(target_ty, val_ty, n.value->span);
                return unit_type();
            } else if constexpr (std::is_same_v<T, ExprBlock>) {
                size_t mark = env_.mark();
                size_t mut_mark = mut_env_.mark();
                TypePtr last_ty = unit_type();
                for (size_t i = 0; i < n.stmts.size(); ++i) {
                    const StmtPtr& s = n.stmts[i];
                    bool is_last = (i + 1 == n.stmts.size());
                    if (auto* se = std::get_if<StmtExpr>(&s->node)) {
                        TypePtr t = infer(se->expr);
                        if (is_last) last_ty = resolve(t);
                    } else if (auto* sv = std::get_if<StmtVar>(&s->node)) {
                        TypePtr val_ty = infer(sv->value);
                        if (sv->type_ann) {
                            std::unordered_map<std::string, TypePtr> tvars;
                            TypePtr ann_ty = resolve_type_expr(*sv->type_ann, tvars, s->span);
                            unify(val_ty, ann_ty, sv->value->span);
                        }
                        env_.push(sv->name, TypeScheme{{}, resolve(val_ty)});
                        mut_env_.push(sv->name, true);
                    } else if (auto* sl = std::get_if<StmtLet>(&s->node)) {
                        if (sl->is_rec) {
                            auto* pv = std::get_if<PatVar>(&sl->pat->node);
                            if (!pv) error("'let rec' requires a simple variable", sl->pat->span);
                            std::set<uint32_t> outer_fv = env_free_vars();
                            TypePtr rec_ty = gen_.fresh();
                            size_t m2 = env_.mark();
                            size_t mm2 = mut_env_.mark();
                            env_.push(pv->ident, TypeScheme{{}, rec_ty});
                            mut_env_.push(pv->ident, false);
                            TypePtr vt = infer(sl->value);
                            unify(rec_ty, vt, sl->value->span);
                            env_.restore(m2);
                            mut_env_.restore(mm2);
                            env_.push(pv->ident, generalize(outer_fv, resolve(rec_ty)));
                            mut_env_.push(pv->ident, false);
                        } else {
                            TypePtr vt = infer(sl->value);
                            if (sl->type_ann) {
                                std::unordered_map<std::string, TypePtr> tvars;
                                TypePtr ann_ty = resolve_type_expr(*sl->type_ann, tvars, s->span);
                                unify(vt, ann_ty, sl->value->span);
                            }
                            PatResult pr = infer_pattern(sl->pat);
                            unify(pr.type, vt, sl->pat->span);
                            std::set<uint32_t> outer_fv = env_free_vars();
                            for (const auto& [id, ty] : pr.bindings) {
                                env_.push(id, generalize(outer_fv, resolve(ty)));
                                mut_env_.push(id, false);
                            }
                        }
                    } else {
                        check_stmt(s);
                    }
                }
                env_.restore(mark);
                mut_env_.restore(mut_mark);
                return last_ty;
            } else if constexpr (std::is_same_v<T, ExprWhile>) {
                TypePtr cond_ty = infer(n.cond);
                unify(cond_ty, bool_type(), n.cond->span);
                ++loop_depth_;
                (void)infer(n.body);
                --loop_depth_;
                return unit_type();
            } else if constexpr (std::is_same_v<T, ExprFor>) {
                size_t mark = env_.mark();
                size_t mut_mark = mut_env_.mark();
                if (n.init) {
                    const StmtPtr& init_s = *n.init;
                    if (auto* sv = std::get_if<StmtVar>(&init_s->node)) {
                        TypePtr vt = infer(sv->value);
                        env_.push(sv->name, TypeScheme{{}, resolve(vt)});
                        mut_env_.push(sv->name, true);
                    } else if (auto* se = std::get_if<StmtExpr>(&init_s->node)) {
                        (void)infer(se->expr);
                    }
                }
                TypePtr cond_ty = infer(n.cond);
                unify(cond_ty, bool_type(), n.cond->span);
                if (n.step) {
                    (void)infer(*n.step);
                }
                ++loop_depth_;
                (void)infer(n.body);
                --loop_depth_;
                env_.restore(mark);
                mut_env_.restore(mut_mark);
                return unit_type();
            } else if constexpr (std::is_same_v<T, ExprForEach>) {
                TypePtr iter_ty = resolve(infer(n.iterable));
                TypePtr elem_ty;
                if (auto* tc = std::get_if<TypeCon>(&iter_ty->node)) {
                    if ((tc->name == "List" || tc->name == "Vector" || tc->name == "Set") &&
                        tc->args.size() == 1) {
                        elem_ty = tc->args[0];
                    } else if (tc->name == "Dict" && tc->args.size() == 2) {
                        elem_ty = make_ttuple({tc->args[0], tc->args[1]});
                    } else if (tc->name == "String" && tc->args.empty()) {
                        elem_ty = string_type();
                    } else {
                        error("type '" + type_to_string(iter_ty) + "' is not iterable in 'for each'",
                              n.iterable->span);
                    }
                } else if (std::holds_alternative<TypeVar>(iter_ty->node)) {
                    elem_ty = gen_.fresh();
                    unify(iter_ty, list_type(elem_ty), n.iterable->span);
                } else {
                    error("type '" + type_to_string(iter_ty) + "' is not iterable in 'for each'",
                          n.iterable->span);
                }
                size_t mark = env_.mark();
                size_t mut_mark = mut_env_.mark();
                PatResult pr = infer_pattern(n.pat);
                unify(pr.type, elem_ty, n.pat->span);
                for (const auto& [id, ty] : pr.bindings) {
                    env_.push(id, TypeScheme{{}, resolve(ty)});
                    mut_env_.push(id, false);
                }
                ++loop_depth_;
                (void)infer(n.body);
                --loop_depth_;
                env_.restore(mark);
                mut_env_.restore(mut_mark);
                return unit_type();
            } else if constexpr (std::is_same_v<T, ExprBreak>) {
                if (loop_depth_ <= 0) error("'break' used outside of loop", expr->span);
                return gen_.fresh();
            } else if constexpr (std::is_same_v<T, ExprContinue>) {
                if (loop_depth_ <= 0) error("'continue' used outside of loop", expr->span);
                return gen_.fresh();
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
                size_t mut_mark = mut_env_.mark();
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
                    mut_env_.push(pv->ident, false);
                    TypePtr val_ty = infer(n.value);
                    unify(rec_ty, val_ty, n.value->span);
                    env_.restore(mark);
                    mut_env_.restore(mut_mark);
                    TypeScheme scheme = generalize(outer_fv, resolve(rec_ty));
                    env_.push(pv->ident, scheme);
                    mut_env_.push(pv->ident, false);
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
                        mut_env_.push(id, false);
                    }
                }
                TypePtr body_ty = infer(n.body);
                env_.restore(mark);
                mut_env_.restore(mut_mark);
                return resolve(body_ty);
            } else if constexpr (std::is_same_v<T, ExprFn>) {
                size_t mark = env_.mark();
                size_t mut_mark = mut_env_.mark();
                PatResult pat_res = infer_pattern(n.param);
                if (n.param_type) {
                    std::unordered_map<std::string, TypePtr> tvars;
                    TypePtr ann_ty = resolve_type_expr(*n.param_type, tvars, expr->span);
                    unify(pat_res.type, ann_ty, n.param->span);
                }
                for (const auto& [id, ty] : pat_res.bindings) {
                    env_.push(id, TypeScheme{{}, resolve(ty)});
                    mut_env_.push(id, false);
                }
                TypePtr body_ty = infer(n.body);
                env_.restore(mark);
                mut_env_.restore(mut_mark);
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
    switch (u.op) {
        case UnaryOp::NegInt:
        case UnaryOp::BitNot:
            constrain_int_type(operand_ty, span);
            return resolve(operand_ty);
        case UnaryOp::NegFloat:
            constrain_float_type(operand_ty, span);
            return resolve(operand_ty);
        case UnaryOp::LogicalNot:
            unify(operand_ty, bool_type(), span);
            return bool_type();
        case UnaryOp::AddrOf:
            return ptr_type(resolve(operand_ty));
        case UnaryOp::Deref: {
            TypePtr inner = gen_.fresh();
            unify(operand_ty, ptr_type(inner), span);
            return resolve(inner);
        }
    }
    return unit_type();
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
        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
        case BinaryOp::Shl:
        case BinaryOp::Shr:
            unify(lt, rt, span);
            constrain_int_type(lt, span);
            return resolve(lt);

        case BinaryOp::AddFloat:
        case BinaryOp::SubFloat:
        case BinaryOp::MulFloat:
        case BinaryOp::DivFloat:
            unify(lt, rt, span);
            constrain_float_type(lt, span);
            return resolve(lt);

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
        size_t mut_mark = mut_env_.mark();
        PatResult pat_res = infer_pattern(arm.pat);
        unify(scrut_ty, pat_res.type, arm.pat->span);
        for (const auto& [id, ty] : pat_res.bindings) {
            env_.push(id, TypeScheme{{}, resolve(ty)});
            mut_env_.push(id, false);
        }
        TypePtr arm_ty = infer(arm.body);
        unify(result_ty, arm_ty, arm.body->span);
        env_.restore(mark);
        mut_env_.restore(mut_mark);
    }
    return resolve(result_ty);
}

}  // namespace cml
