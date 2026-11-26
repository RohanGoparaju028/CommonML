#include "cml/type.hpp"

namespace cml {

bool occurs_in(uint32_t var_id, const TypePtr& t) {
    if (!t) return false;
    return std::visit(
        [var_id](const auto& n) -> bool {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, TypeVar>) {
                return n.id == var_id;
            } else if constexpr (std::is_same_v<T, TypeCon>) {
                for (const auto& a : n.args) {
                    if (occurs_in(var_id, a)) return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, TypeFun>) {
                return occurs_in(var_id, n.param) || occurs_in(var_id, n.ret);
            } else {
                for (const auto& e : n.elems) {
                    if (occurs_in(var_id, e)) return true;
                }
                return false;
            }
        },
        t->node);
}

static void bind_var(uint32_t id, const TypePtr& t, Subst& subst, std::string& error_out) {
    if (auto* tv = std::get_if<TypeVar>(&t->node); tv && tv->id == id) {
        return;
    }
    if (occurs_in(id, t)) {
        error_out = "occurs check failed: infinite recursive type involving " + type_to_string(t);
        return;
    }
    subst[id] = t;
}

void unify_into(const TypePtr& a_in, const TypePtr& b_in, Subst& subst, std::string& error_out) {
    if (!error_out.empty()) return;
    TypePtr a = apply_subst(subst, a_in);
    TypePtr b = apply_subst(subst, b_in);

    if (auto* va = std::get_if<TypeVar>(&a->node)) {
        bind_var(va->id, b, subst, error_out);
        return;
    }
    if (auto* vb = std::get_if<TypeVar>(&b->node)) {
        bind_var(vb->id, a, subst, error_out);
        return;
    }

    if (auto* fa = std::get_if<TypeFun>(&a->node)) {
        if (auto* fb = std::get_if<TypeFun>(&b->node)) {
            unify_into(fa->param, fb->param, subst, error_out);
            if (!error_out.empty()) return;
            unify_into(fa->ret, fb->ret, subst, error_out);
            return;
        }
    } else if (auto* ca = std::get_if<TypeCon>(&a->node)) {
        if (auto* cb = std::get_if<TypeCon>(&b->node)) {
            if (ca->name == cb->name && ca->args.size() == cb->args.size()) {
                for (size_t i = 0; i < ca->args.size(); ++i) {
                    unify_into(ca->args[i], cb->args[i], subst, error_out);
                    if (!error_out.empty()) return;
                }
                return;
            }
        }
    } else if (auto* ta = std::get_if<TypeTuple>(&a->node)) {
        if (auto* tb = std::get_if<TypeTuple>(&b->node)) {
            if (ta->elems.size() == tb->elems.size()) {
                for (size_t i = 0; i < ta->elems.size(); ++i) {
                    unify_into(ta->elems[i], tb->elems[i], subst, error_out);
                    if (!error_out.empty()) return;
                }
                return;
            }
        }
    }

    error_out = "type mismatch: cannot unify '" + type_to_string(a) + "' with '" +
                type_to_string(b) + "'";
}

static TypePtr apply_renaming(const Subst& s, const TypePtr& t) {
    if (!t) return nullptr;
    return std::visit(
        [&](const auto& n) -> TypePtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, TypeVar>) {
                auto it = s.find(n.id);
                return it != s.end() ? it->second : t;
            } else if constexpr (std::is_same_v<T, TypeCon>) {
                std::vector<TypePtr> args;
                args.reserve(n.args.size());
                for (const auto& a : n.args) args.push_back(apply_renaming(s, a));
                return make_tcon(n.name, std::move(args));
            } else if constexpr (std::is_same_v<T, TypeFun>) {
                return make_tfun(apply_renaming(s, n.param), apply_renaming(s, n.ret));
            } else {
                std::vector<TypePtr> elems;
                elems.reserve(n.elems.size());
                for (const auto& e : n.elems) elems.push_back(apply_renaming(s, e));
                return make_ttuple(std::move(elems));
            }
        },
        t->node);
}

TypePtr instantiate(const TypeScheme& scheme, TypeVarGen& gen) {
    if (scheme.quantified.empty()) {
        return scheme.type;
    }
    Subst s;
    for (uint32_t q : scheme.quantified) {
        s[q] = gen.fresh();
    }
    return apply_renaming(s, scheme.type);
}

TypeScheme generalize(const std::set<uint32_t>& env_free_vars, const TypePtr& t) {
    std::set<uint32_t> fv = free_tvars(t);
    std::vector<uint32_t> quant;
    for (uint32_t id : fv) {
        if (env_free_vars.find(id) == env_free_vars.end()) {
            quant.push_back(id);
        }
    }
    return TypeScheme{std::move(quant), t};
}

}  // namespace cml
