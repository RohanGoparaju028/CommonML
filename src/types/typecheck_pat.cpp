#include "cml/typecheck.hpp"

#include <functional>
#include <unordered_set>

namespace cml {

TypeChecker::PatResult TypeChecker::infer_pattern(const PatternPtr& pat) {
    if (!pat) {
        return PatResult{gen_.fresh(), {}};
    }

    std::unordered_set<std::string> seen_vars;

    auto check_unique = [&](const Ident& id) {
        // Scope-aware uniqueness key could use scopes, but within one pattern name uniqueness is standard.
        if (!seen_vars.insert(id.name).second) {
            error("duplicate variable '" + id.name + "' in pattern", id.span);
        }
    };

    std::function<TypePtr(const PatternPtr&, std::vector<std::pair<Ident, TypePtr>>&)> walk;
    walk = [&](const PatternPtr& p, std::vector<std::pair<Ident, TypePtr>>& bindings) -> TypePtr {
        return std::visit(
            [&](const auto& n) -> TypePtr {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, PatWildcard>) {
                    return gen_.fresh();
                } else if constexpr (std::is_same_v<T, PatVar>) {
                    check_unique(n.ident);
                    TypePtr tv = gen_.fresh();
                    bindings.emplace_back(n.ident, tv);
                    return tv;
                } else if constexpr (std::is_same_v<T, PatInt>) {
                    return int_type();
                } else if constexpr (std::is_same_v<T, PatFloat>) {
                    return float_type();
                } else if constexpr (std::is_same_v<T, PatBool>) {
                    return bool_type();
                } else if constexpr (std::is_same_v<T, PatString>) {
                    return string_type();
                } else if constexpr (std::is_same_v<T, PatUnit>) {
                    return unit_type();
                } else if constexpr (std::is_same_v<T, PatTuple>) {
                    std::vector<TypePtr> elems;
                    elems.reserve(n.elems.size());
                    for (const auto& e : n.elems) {
                        elems.push_back(walk(e, bindings));
                    }
                    return make_ttuple(std::move(elems));
                } else if constexpr (std::is_same_v<T, PatList>) {
                    TypePtr elem_ty = gen_.fresh();
                    for (const auto& e : n.elems) {
                        TypePtr t = walk(e, bindings);
                        unify(elem_ty, t, e->span);
                    }
                    return list_type(resolve(elem_ty));
                } else if constexpr (std::is_same_v<T, PatCons>) {
                    TypePtr head_ty = walk(n.head, bindings);
                    TypePtr tail_ty = walk(n.tail, bindings);
                    unify(tail_ty, list_type(head_ty), p->span);
                    return resolve(tail_ty);
                } else if constexpr (std::is_same_v<T, PatCtor>) {
                    auto it = ctors_.find(n.ctor.name);
                    if (it == ctors_.end()) {
                        error("unknown constructor '" + n.ctor.name + "'", n.ctor.span);
                    }
                    const CtorInfo& info = it->second;
                    if (n.args.size() != info.arity) {
                        error("constructor '" + n.ctor.name + "' expects " +
                                  std::to_string(info.arity) + " argument(s), got " +
                                  std::to_string(n.args.size()),
                              p->span);
                    }
                    TypePtr ctor_ty = instantiate(info.scheme, gen_);
                    for (const auto& arg : n.args) {
                        auto* fn_ty = std::get_if<TypeFun>(&ctor_ty->node);
                        if (!fn_ty) {
                            error("internal error: constructor type is not a function", p->span);
                        }
                        TypePtr arg_ty = walk(arg, bindings);
                        unify(fn_ty->param, arg_ty, arg->span);
                        ctor_ty = resolve(fn_ty->ret);
                    }
                    return resolve(ctor_ty);
                } else {
                    error("unquote pattern outside macro expansion", p->span);
                }
            },
            p->node);
    };

    PatResult res;
    res.type = walk(pat, res.bindings);
    return res;
}

}  // namespace cml
