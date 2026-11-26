#include "cml/value.hpp"
#include "cml/bytecode.hpp"
#include "cml/vm.hpp"

#include <algorithm>
#include <sstream>

namespace cml {

namespace {

bool values_equal_depth(const Value& a, const Value& b, int depth) {
    if (depth > 64) return false;
    if (a.node.index() != b.node.index()) return false;
    return std::visit(
        [&](const auto& va) -> bool {
            using T = std::decay_t<decltype(va)>;
            const auto& vb = std::get<T>(b.node);
            if constexpr (std::is_same_v<T, ValUnit>) {
                return true;
            } else if constexpr (std::is_same_v<T, ValInt>) {
                return va.value == vb.value;
            } else if constexpr (std::is_same_v<T, ValFloat>) {
                return va.value == vb.value;
            } else if constexpr (std::is_same_v<T, ValBool>) {
                return va.value == vb.value;
            } else if constexpr (std::is_same_v<T, ValString>) {
                return va.value == vb.value;
            } else if constexpr (std::is_same_v<T, ValTuple>) {
                if (va.ptr == vb.ptr) return true;
                if (va.ptr->elems.size() != vb.ptr->elems.size()) return false;
                for (size_t i = 0; i < va.ptr->elems.size(); ++i) {
                    if (!values_equal_depth(va.ptr->elems[i], vb.ptr->elems[i], depth + 1)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, ValList>) {
                if (va.ptr == vb.ptr) return true;
                if (va.ptr->elems.size() != vb.ptr->elems.size()) return false;
                for (size_t i = 0; i < va.ptr->elems.size(); ++i) {
                    if (!values_equal_depth(va.ptr->elems[i], vb.ptr->elems[i], depth + 1)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, ValAdt>) {
                if (va.ptr == vb.ptr) return true;
                if (va.ptr->tag != vb.ptr->tag || va.ptr->ctor_name != vb.ptr->ctor_name ||
                    va.ptr->fields.size() != vb.ptr->fields.size()) {
                    return false;
                }
                for (size_t i = 0; i < va.ptr->fields.size(); ++i) {
                    if (!values_equal_depth(va.ptr->fields[i], vb.ptr->fields[i], depth + 1)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, ValStruct>) {
                if (va.ptr == vb.ptr) return true;
                if (va.ptr->type_name != vb.ptr->type_name ||
                    va.ptr->fields.size() != vb.ptr->fields.size()) {
                    return false;
                }
                for (const auto& [k, v] : va.ptr->fields) {
                    auto it = vb.ptr->fields.find(k);
                    if (it == vb.ptr->fields.end() ||
                        !values_equal_depth(v, it->second, depth + 1)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, ValVector>) {
                if (va.ptr == vb.ptr) return true;
                if (va.ptr->elems.size() != vb.ptr->elems.size()) return false;
                for (size_t i = 0; i < va.ptr->elems.size(); ++i) {
                    if (!values_equal_depth(va.ptr->elems[i], vb.ptr->elems[i], depth + 1)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, ValDict>) {
                if (va.ptr == vb.ptr) return true;
                if (va.ptr->entries.size() != vb.ptr->entries.size()) return false;
                for (const auto& [k, v] : va.ptr->entries) {
                    bool found = false;
                    for (const auto& [k2, v2] : vb.ptr->entries) {
                        if (values_equal_depth(k, k2, depth + 1) &&
                            values_equal_depth(v, v2, depth + 1)) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, ValSet>) {
                if (va.ptr == vb.ptr) return true;
                if (va.ptr->elems.size() != vb.ptr->elems.size()) return false;
                for (const auto& e : va.ptr->elems) {
                    bool found = false;
                    for (const auto& e2 : vb.ptr->elems) {
                        if (values_equal_depth(e, e2, depth + 1)) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, ValPtr>) {
                return va.ptr == vb.ptr;
            } else if constexpr (std::is_same_v<T, ValClosure>) {
                return va.proto == vb.proto && va.upvalues == vb.upvalues;
            } else if constexpr (std::is_same_v<T, ValNative>) {
                return va.name == vb.name && va.bound_args.size() == vb.bound_args.size();
            } else if constexpr (std::is_same_v<T, ValCtor>) {
                return va.tag == vb.tag && va.name == vb.name &&
                       va.bound_args.size() == vb.bound_args.size();
            } else if constexpr (std::is_same_v<T, ValAstExpr>) {
                return va.expr == vb.expr;
            } else {
                return va.pat == vb.pat;
            }
        },
        a.node);
}

}  // namespace

bool values_equal(const Value& a, const Value& b) {
    return values_equal_depth(a, b, 0);
}

int compare_values(const Value& a, const Value& b) {
    if (a.node.index() != b.node.index()) {
        return a.node.index() < b.node.index() ? -1 : 1;
    }
    return std::visit(
        [&](const auto& va) -> int {
            using T = std::decay_t<decltype(va)>;
            const auto& vb = std::get<T>(b.node);
            if constexpr (std::is_same_v<T, ValUnit>) {
                return 0;
            } else if constexpr (std::is_same_v<T, ValInt>) {
                return va.value < vb.value ? -1 : (va.value > vb.value ? 1 : 0);
            } else if constexpr (std::is_same_v<T, ValFloat>) {
                return va.value < vb.value ? -1 : (va.value > vb.value ? 1 : 0);
            } else if constexpr (std::is_same_v<T, ValBool>) {
                return static_cast<int>(va.value) - static_cast<int>(vb.value);
            } else if constexpr (std::is_same_v<T, ValString>) {
                return va.value < vb.value ? -1 : (va.value > vb.value ? 1 : 0);
            } else if constexpr (std::is_same_v<T, ValTuple>) {
                size_t n = std::min(va.ptr->elems.size(), vb.ptr->elems.size());
                for (size_t i = 0; i < n; ++i) {
                    int c = compare_values(va.ptr->elems[i], vb.ptr->elems[i]);
                    if (c != 0) return c;
                }
                return va.ptr->elems.size() < vb.ptr->elems.size()
                           ? -1
                           : (va.ptr->elems.size() > vb.ptr->elems.size() ? 1 : 0);
            } else if constexpr (std::is_same_v<T, ValList>) {
                size_t n = std::min(va.ptr->elems.size(), vb.ptr->elems.size());
                for (size_t i = 0; i < n; ++i) {
                    int c = compare_values(va.ptr->elems[i], vb.ptr->elems[i]);
                    if (c != 0) return c;
                }
                return va.ptr->elems.size() < vb.ptr->elems.size()
                           ? -1
                           : (va.ptr->elems.size() > vb.ptr->elems.size() ? 1 : 0);
            } else if constexpr (std::is_same_v<T, ValAdt>) {
                if (va.ptr->tag != vb.ptr->tag) return va.ptr->tag < vb.ptr->tag ? -1 : 1;
                size_t n = std::min(va.ptr->fields.size(), vb.ptr->fields.size());
                for (size_t i = 0; i < n; ++i) {
                    int c = compare_values(va.ptr->fields[i], vb.ptr->fields[i]);
                    if (c != 0) return c;
                }
                return va.ptr->fields.size() < vb.ptr->fields.size()
                           ? -1
                           : (va.ptr->fields.size() > vb.ptr->fields.size() ? 1 : 0);
            } else {
                return 0;
            }
        },
        a.node);
}

static std::string format_elem(const Value& v) {
    if (std::holds_alternative<ValString>(v.node)) {
        return "\"" + value_to_string(v) + "\"";
    }
    return value_to_string(v);
}

std::string value_to_string(const Value& v) {
    return std::visit(
        [](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ValUnit>) {
                return "()";
            } else if constexpr (std::is_same_v<T, ValInt>) {
                return std::to_string(n.value);
            } else if constexpr (std::is_same_v<T, ValFloat>) {
                std::ostringstream os;
                os << n.value;
                return os.str();
            } else if constexpr (std::is_same_v<T, ValBool>) {
                return n.value ? "true" : "false";
            } else if constexpr (std::is_same_v<T, ValString>) {
                return n.value;
            } else if constexpr (std::is_same_v<T, ValTuple>) {
                std::string s = "(";
                for (size_t i = 0; i < n.ptr->elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += format_elem(n.ptr->elems[i]);
                }
                s += ")";
                return s;
            } else if constexpr (std::is_same_v<T, ValList>) {
                std::string s = "[";
                for (size_t i = 0; i < n.ptr->elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += format_elem(n.ptr->elems[i]);
                }
                s += "]";
                return s;
            } else if constexpr (std::is_same_v<T, ValAdt>) {
                if (n.ptr->fields.empty()) return n.ptr->ctor_name;
                std::string s = n.ptr->ctor_name;
                for (const auto& f : n.ptr->fields) {
                    s += " ";
                    bool wrap = std::holds_alternative<ValAdt>(f.node) &&
                                !std::get<ValAdt>(f.node).ptr->fields.empty();
                    if (wrap) s += "(";
                    s += format_elem(f);
                    if (wrap) s += ")";
                }
                return s;
            } else if constexpr (std::is_same_v<T, ValStruct>) {
                std::vector<std::string> keys;
                keys.reserve(n.ptr->fields.size());
                for (const auto& [k, _] : n.ptr->fields) keys.push_back(k);
                std::sort(keys.begin(), keys.end());
                std::string s = n.ptr->type_name + " { ";
                for (size_t i = 0; i < keys.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += keys[i] + " = " + format_elem(n.ptr->fields.at(keys[i]));
                }
                s += " }";
                return s;
            } else if constexpr (std::is_same_v<T, ValVector>) {
                std::string s = "Vector[";
                for (size_t i = 0; i < n.ptr->elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += format_elem(n.ptr->elems[i]);
                }
                s += "]";
                return s;
            } else if constexpr (std::is_same_v<T, ValDict>) {
                std::string s = "Dict{";
                for (size_t i = 0; i < n.ptr->entries.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += format_elem(n.ptr->entries[i].first) + " -> " +
                         format_elem(n.ptr->entries[i].second);
                }
                s += "}";
                return s;
            } else if constexpr (std::is_same_v<T, ValSet>) {
                std::string s = "Set{";
                for (size_t i = 0; i < n.ptr->elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += format_elem(n.ptr->elems[i]);
                }
                s += "}";
                return s;
            } else if constexpr (std::is_same_v<T, ValPtr>) {
                if (!n.ptr || !n.ptr->alive) return "<closed ptr>";
                if (n.ptr->is_stack_slot) return "<stack ptr>";
                return "&(" + format_elem(n.ptr->value) + ")";
            } else if constexpr (std::is_same_v<T, ValClosure>) {
                return "<fn " + (n.proto ? n.proto->name : "lambda") + ">";
            } else if constexpr (std::is_same_v<T, ValNative>) {
                return "<builtin " + n.name + ">";
            } else if constexpr (std::is_same_v<T, ValCtor>) {
                return "<ctor " + n.name + ">";
            } else if constexpr (std::is_same_v<T, ValAstExpr>) {
                return "<ast " + expr_to_string(n.expr) + ">";
            } else {
                return "<pat " + pattern_to_string(n.pat) + ">";
            }
        },
        v.node);
}

bool is_truthy(const Value& v) {
    if (auto* b = std::get_if<ValBool>(&v.node)) {
        return b->value;
    }
    return false;
}

size_t value_ref_count(const Value& v) {
    return std::visit(
        [](const auto& n) -> size_t {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ValTuple> || std::is_same_v<T, ValList> ||
                          std::is_same_v<T, ValAdt> || std::is_same_v<T, ValStruct> ||
                          std::is_same_v<T, ValVector> || std::is_same_v<T, ValDict> ||
                          std::is_same_v<T, ValSet> || std::is_same_v<T, ValPtr>) {
                return n.ptr.use_count();
            } else {
                return 1;
            }
        },
        v.node);
}

}  // namespace cml
