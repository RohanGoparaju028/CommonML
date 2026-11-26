#include "cml/typecheck.hpp"
#include "cml/builtins.hpp"

#include <unordered_set>

namespace cml {

TypeChecker::TypeChecker() {
    register_builtins();
}

void TypeChecker::register_builtins() {
    type_decls_["Int"] = TypeDeclInfo{"Int", 0, {}};
    type_decls_["i8"] = TypeDeclInfo{"i8", 0, {}};
    type_decls_["i16"] = TypeDeclInfo{"i16", 0, {}};
    type_decls_["i32"] = TypeDeclInfo{"i32", 0, {}};
    type_decls_["i64"] = TypeDeclInfo{"i64", 0, {}};
    type_decls_["isize"] = TypeDeclInfo{"isize", 0, {}};
    type_decls_["u8"] = TypeDeclInfo{"u8", 0, {}};
    type_decls_["u16"] = TypeDeclInfo{"u16", 0, {}};
    type_decls_["u32"] = TypeDeclInfo{"u32", 0, {}};
    type_decls_["u64"] = TypeDeclInfo{"u64", 0, {}};
    type_decls_["usize"] = TypeDeclInfo{"usize", 0, {}};
    type_decls_["Float"] = TypeDeclInfo{"Float", 0, {}};
    type_decls_["f32"] = TypeDeclInfo{"f32", 0, {}};
    type_decls_["f64"] = TypeDeclInfo{"f64", 0, {}};
    type_decls_["Bool"] = TypeDeclInfo{"Bool", 0, {}};
    type_decls_["String"] = TypeDeclInfo{"String", 0, {}};
    type_decls_["Unit"] = TypeDeclInfo{"Unit", 0, {}};
    type_decls_["Ast"] = TypeDeclInfo{"Ast", 0, {}};
    type_decls_["List"] = TypeDeclInfo{"List", 1, {}};
    type_decls_["Vector"] = TypeDeclInfo{"Vector", 1, {}};
    type_decls_["Dict"] = TypeDeclInfo{"Dict", 2, {}};
    type_decls_["Set"] = TypeDeclInfo{"Set", 1, {}};
    type_decls_["Ptr"] = TypeDeclInfo{"Ptr", 1, {}};

    for (const auto& b : get_builtins()) {
        env_.push(Ident{b.name, {}, {}}, b.scheme);
        mut_env_.push(Ident{b.name, {}, {}}, false);
    }
}

void TypeChecker::register_struct_info(const StructInfo& info) {
    type_decls_[info.name] = TypeDeclInfo{info.name, info.type_params.size(), {}};
    structs_[info.name] = info;
}

void TypeChecker::register_struct_decl(const StmtStructDecl& decl, const SourceSpan& span) {
    if (type_decls_.count(decl.name.name)) {
        error("type '" + decl.name.name + "' is already defined", span);
    }

    std::unordered_map<std::string, TypePtr> tvars;
    std::vector<uint32_t> param_ids;
    for (const auto& tp : decl.type_params) {
        if (tvars.count(tp.name)) {
            error("duplicate type parameter '" + tp.name + "' in struct '" + decl.name.name + "'",
                  tp.span);
        }
        uint32_t id = gen_.next_id();
        tvars[tp.name] = make_tvar(id);
        param_ids.push_back(id);
    }

    StructInfo info;
    info.name = decl.name.name;
    info.type_params = std::move(param_ids);
    std::unordered_set<std::string> seen_fields;
    for (const auto& f : decl.fields) {
        if (!seen_fields.insert(f.name.name).second) {
            error("duplicate field '" + f.name.name + "' in struct '" + decl.name.name + "'",
                  f.span);
        }
        TypePtr fty = resolve_type_expr(f.type, tvars, f.span);
        info.fields.emplace_back(f.name.name, std::move(fty));
    }

    register_struct_info(info);
}

void TypeChecker::error(const std::string& msg, const SourceSpan& span) const {
    throw CompileError(Diagnostic{"type", msg, span});
}

TypePtr TypeChecker::resolve(const TypePtr& t) const {
    return apply_subst(subst_, t);
}

void TypeChecker::unify(const TypePtr& a, const TypePtr& b, const SourceSpan& span) {
    std::string err;
    unify_into(a, b, subst_, err);
    if (!err.empty()) {
        error(err, span);
    }
}

std::set<uint32_t> TypeChecker::env_free_vars() const {
    std::set<uint32_t> out;
    for (const auto& entry : env_.entries()) {
        collect_free_tvars_scheme_resolved(entry.value, subst_, out);
    }
    return out;
}

TypePtr TypeChecker::resolve_type_expr(const TypeExprPtr& te,
                                         const std::unordered_map<std::string, TypePtr>& tvars,
                                         const SourceSpan& fallback_span) const {
    if (!te) error("missing type expression", fallback_span);
    return std::visit(
        [&](const auto& n) -> TypePtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, TVar>) {
                auto it = tvars.find(n.name);
                if (it != tvars.end()) return it->second;
                if (n.name == "bool") return bool_type();
                if (n.name == "str" || n.name == "string") return string_type();
                if (n.name == "unit" || n.name == "void") return unit_type();
                if (n.name == "int") return int_type();
                if (n.name == "float") return float_type();
                auto dit = type_decls_.find(n.name);
                if (dit != type_decls_.end() && dit->second.arity == 0) {
                    return make_tcon(n.name);
                }
                error("unbound type variable '" + n.name + "'", te->span);
            } else if constexpr (std::is_same_v<T, TPtr>) {
                return ptr_type(resolve_type_expr(n.inner, tvars, te->span));
            } else if constexpr (std::is_same_v<T, TCon>) {
                auto it = type_decls_.find(n.name);
                if (it == type_decls_.end()) {
                    error("unknown type constructor '" + n.name + "'", te->span);
                }
                if (n.args.size() != it->second.arity) {
                    error("type '" + n.name + "' expects " + std::to_string(it->second.arity) +
                              " type argument(s), got " + std::to_string(n.args.size()),
                          te->span);
                }
                std::vector<TypePtr> args;
                args.reserve(n.args.size());
                for (const auto& a : n.args) {
                    args.push_back(resolve_type_expr(a, tvars, te->span));
                }
                return make_tcon(n.name, std::move(args));
            } else if constexpr (std::is_same_v<T, TFun>) {
                return make_tfun(resolve_type_expr(n.param, tvars, te->span),
                                 resolve_type_expr(n.ret, tvars, te->span));
            } else {
                std::vector<TypePtr> elems;
                elems.reserve(n.elems.size());
                for (const auto& e : n.elems) {
                    elems.push_back(resolve_type_expr(e, tvars, te->span));
                }
                return make_ttuple(std::move(elems));
            }
        },
        te->node);
}

void TypeChecker::register_type_decl(const StmtTypeDecl& decl, const SourceSpan& span) {
    if (type_decls_.count(decl.name.name)) {
        error("type '" + decl.name.name + "' is already defined", span);
    }

    std::unordered_map<std::string, TypePtr> tvars;
    std::vector<TypePtr> param_types;
    std::vector<uint32_t> quant_ids;

    for (const auto& tp : decl.type_params) {
        if (tvars.count(tp.name)) {
            error("duplicate type parameter '" + tp.name + "' in type '" + decl.name.name + "'",
                  tp.span);
        }
        uint32_t id = gen_.next_id();
        TypePtr tv = make_tvar(id);
        tvars[tp.name] = tv;
        param_types.push_back(tv);
        quant_ids.push_back(id);
    }

    TypeDeclInfo info;
    info.name = decl.name.name;
    info.arity = decl.type_params.size();
    for (const auto& c : decl.ctors) {
        info.ctors.push_back(c.name.name);
    }
    // Insert early so recursive ADTs (e.g. Tree a = Leaf | Node (Tree a) a (Tree a)) resolve.
    type_decls_[decl.name.name] = info;

    TypePtr result_type = make_tcon(decl.name.name, std::move(param_types));
    std::unordered_set<std::string> seen_ctors;

    uint32_t tag = 0;
    for (const auto& ctor : decl.ctors) {
        if (!seen_ctors.insert(ctor.name.name).second) {
            error("duplicate constructor '" + ctor.name.name + "' in type '" + decl.name.name + "'",
                  ctor.span);
        }
        if (ctors_.count(ctor.name.name)) {
            error("constructor '" + ctor.name.name + "' is already defined", ctor.span);
        }
        TypePtr ctor_type = result_type;
        for (auto it = ctor.fields.rbegin(); it != ctor.fields.rend(); ++it) {
            TypePtr field_ty = resolve_type_expr(*it, tvars, ctor.span);
            ctor_type = make_tfun(field_ty, ctor_type);
        }
        TypeScheme scheme{quant_ids, ctor_type};
        ctors_[ctor.name.name] = CtorInfo{decl.name.name, tag++, ctor.fields.size(), scheme};
        env_.push(ctor.name, scheme);
    }
}

}  // namespace cml
