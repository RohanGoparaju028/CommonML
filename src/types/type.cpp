#include "cml/type.hpp"

#include <sstream>
#include <unordered_map>

namespace cml {

TypePtr make_tvar(uint32_t id) {
    return std::make_shared<Type>(Type{TypeVar{id}});
}

TypePtr make_tcon(std::string name, std::vector<TypePtr> args) {
    return std::make_shared<Type>(Type{TypeCon{std::move(name), std::move(args)}});
}

TypePtr make_tfun(TypePtr param, TypePtr ret) {
    return std::make_shared<Type>(Type{TypeFun{std::move(param), std::move(ret)}});
}

TypePtr make_ttuple(std::vector<TypePtr> elems) {
    return std::make_shared<Type>(Type{TypeTuple{std::move(elems)}});
}

TypePtr int_type() {
    static const TypePtr t = make_tcon("Int");
    return t;
}

TypePtr float_type() {
    static const TypePtr t = make_tcon("Float");
    return t;
}

TypePtr bool_type() {
    static const TypePtr t = make_tcon("Bool");
    return t;
}

TypePtr string_type() {
    static const TypePtr t = make_tcon("String");
    return t;
}

TypePtr unit_type() {
    static const TypePtr t = make_tcon("Unit");
    return t;
}

TypePtr ast_type() {
    static const TypePtr t = make_tcon("Ast");
    return t;
}

TypePtr list_type(TypePtr elem) {
    return make_tcon("List", {std::move(elem)});
}

TypePtr apply_subst(const Subst& s, const TypePtr& t) {
    if (!t) return nullptr;
    return std::visit(
        [&](const auto& n) -> TypePtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, TypeVar>) {
                auto it = s.find(n.id);
                if (it != s.end()) {
                    return apply_subst(s, it->second);
                }
                return t;
            } else if constexpr (std::is_same_v<T, TypeCon>) {
                std::vector<TypePtr> args;
                args.reserve(n.args.size());
                for (const auto& a : n.args) {
                    args.push_back(apply_subst(s, a));
                }
                return make_tcon(n.name, std::move(args));
            } else if constexpr (std::is_same_v<T, TypeFun>) {
                return make_tfun(apply_subst(s, n.param), apply_subst(s, n.ret));
            } else {
                std::vector<TypePtr> elems;
                elems.reserve(n.elems.size());
                for (const auto& e : n.elems) {
                    elems.push_back(apply_subst(s, e));
                }
                return make_ttuple(std::move(elems));
            }
        },
        t->node);
}

TypeScheme apply_subst_scheme(const Subst& s, const TypeScheme& scheme) {
    Subst filtered = s;
    for (uint32_t q : scheme.quantified) {
        filtered.erase(q);
    }
    return TypeScheme{scheme.quantified, apply_subst(filtered, scheme.type)};
}

Subst compose_subst(const Subst& outer, const Subst& inner) {
    Subst result;
    for (const auto& [k, v] : inner) {
        result[k] = apply_subst(outer, v);
    }
    for (const auto& [k, v] : outer) {
        result.try_emplace(k, v);
    }
    return result;
}

void collect_free_tvars(const TypePtr& t, std::set<uint32_t>& out) {
    if (!t) return;
    std::visit(
        [&](const auto& n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, TypeVar>) {
                out.insert(n.id);
            } else if constexpr (std::is_same_v<T, TypeCon>) {
                for (const auto& a : n.args) collect_free_tvars(a, out);
            } else if constexpr (std::is_same_v<T, TypeFun>) {
                collect_free_tvars(n.param, out);
                collect_free_tvars(n.ret, out);
            } else {
                for (const auto& e : n.elems) collect_free_tvars(e, out);
            }
        },
        t->node);
}

std::set<uint32_t> free_tvars(const TypePtr& t) {
    std::set<uint32_t> out;
    collect_free_tvars(t, out);
    return out;
}

std::set<uint32_t> free_tvars_scheme(const TypeScheme& scheme) {
    auto out = free_tvars(scheme.type);
    for (uint32_t q : scheme.quantified) {
        out.erase(q);
    }
    return out;
}

static std::string format_tvar_name(uint32_t id, std::unordered_map<uint32_t, std::string>& names) {
    auto it = names.find(id);
    if (it != names.end()) return it->second;
    size_t idx = names.size();
    std::string label = "'";
    if (idx < 26) {
        label.push_back(static_cast<char>('a' + idx));
    } else {
        label += "t" + std::to_string(idx);
    }
    names[id] = label;
    return label;
}

static std::string format_type_impl(const TypePtr& t,
                                    std::unordered_map<uint32_t, std::string>& names,
                                    int parent_prec) {
    if (!t) return "?";
    return std::visit(
        [&](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, TypeVar>) {
                return format_tvar_name(n.id, names);
            } else if constexpr (std::is_same_v<T, TypeCon>) {
                if (n.args.empty()) return n.name;
                std::string s = n.name;
                for (const auto& a : n.args) {
                    s += " " + format_type_impl(a, names, 3);
                }
                return parent_prec > 2 ? "(" + s + ")" : s;
            } else if constexpr (std::is_same_v<T, TypeFun>) {
                std::string s = format_type_impl(n.param, names, 1) + " -> " +
                                format_type_impl(n.ret, names, 0);
                return parent_prec > 0 ? "(" + s + ")" : s;
            } else {
                std::string s;
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    if (i > 0) s += " * ";
                    s += format_type_impl(n.elems[i], names, 2);
                }
                return parent_prec > 1 ? "(" + s + ")" : s;
            }
        },
        t->node);
}

std::string type_to_string(const TypePtr& t) {
    std::unordered_map<uint32_t, std::string> names;
    return format_type_impl(t, names, 0);
}

std::string scheme_to_string(const TypeScheme& scheme) {
    return type_to_string(scheme.type);
}

}  // namespace cml
