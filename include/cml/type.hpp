#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cml {

struct Type;
using TypePtr = std::shared_ptr<const Type>;

struct TypeVar {
    uint32_t id = 0;
};

struct TypeCon {
    std::string name;
    std::vector<TypePtr> args;
};

struct TypeFun {
    TypePtr param;
    TypePtr ret;
};

struct TypeTuple {
    std::vector<TypePtr> elems;
};

struct Type {
    std::variant<TypeVar, TypeCon, TypeFun, TypeTuple> node;
};

struct TypeScheme {
    std::vector<uint32_t> quantified;
    TypePtr type;
};

using Subst = std::unordered_map<uint32_t, TypePtr>;

TypePtr make_tvar(uint32_t id);
TypePtr make_tcon(std::string name, std::vector<TypePtr> args = {});
TypePtr make_tfun(TypePtr param, TypePtr ret);
TypePtr make_ttuple(std::vector<TypePtr> elems);

TypePtr int_type();
TypePtr float_type();
TypePtr bool_type();
TypePtr string_type();
TypePtr unit_type();
TypePtr ast_type();
TypePtr list_type(TypePtr elem);
TypePtr vector_type(TypePtr elem);
TypePtr dict_type(TypePtr key, TypePtr val);
TypePtr set_type(TypePtr elem);
TypePtr ptr_type(TypePtr inner);

bool is_int_type_name(const std::string& name);
bool is_float_type_name(const std::string& name);

TypePtr apply_subst(const Subst& s, const TypePtr& t);
TypeScheme apply_subst_scheme(const Subst& s, const TypeScheme& scheme);
Subst compose_subst(const Subst& outer, const Subst& inner);

void collect_free_tvars(const TypePtr& t, std::set<uint32_t>& out);
void collect_free_tvars_scheme_resolved(const TypeScheme& scheme, const Subst& s,
                                        std::set<uint32_t>& out);
std::set<uint32_t> free_tvars(const TypePtr& t);
std::set<uint32_t> free_tvars_scheme(const TypeScheme& scheme);

std::string type_to_string(const TypePtr& t);
std::string scheme_to_string(const TypeScheme& scheme);

class TypeVarGen {
public:
    uint32_t next_id() { return next_++; }
    TypePtr fresh() { return make_tvar(next_++); }
    void reset() { next_ = 0; }

private:
    uint32_t next_ = 0;
};

// Unification helpers (implemented in src/types/unify.cpp)
bool occurs_in(uint32_t var_id, const TypePtr& t);
void unify_into(const TypePtr& a, const TypePtr& b, Subst& subst, std::string& error_out);
TypePtr instantiate(const TypeScheme& scheme, TypeVarGen& gen);
TypeScheme generalize(const std::set<uint32_t>& env_free_vars, const TypePtr& t);

}  // namespace cml
