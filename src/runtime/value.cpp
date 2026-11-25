#include "cml/value.hpp"
#include "cml/bytecode.hpp"

#include <sstream>

namespace cml {

bool values_equal(const Value& a, const Value& b) {
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
                if (va.elems.size() != vb.elems.size()) return false;
                for (size_t i = 0; i < va.elems.size(); ++i) {
                    if (!values_equal(va.elems[i], vb.elems[i])) return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, ValList>) {
                if (va.elems.size() != vb.elems.size()) return false;
                for (size_t i = 0; i < va.elems.size(); ++i) {
                    if (!values_equal(va.elems[i], vb.elems[i])) return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, ValAdt>) {
                if (va.tag != vb.tag || va.ctor_name != vb.ctor_name ||
                    va.fields.size() != vb.fields.size()) {
                    return false;
                }
                for (size_t i = 0; i < va.fields.size(); ++i) {
                    if (!values_equal(va.fields[i], vb.fields[i])) return false;
                }
                return true;
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
                size_t n = std::min(va.elems.size(), vb.elems.size());
                for (size_t i = 0; i < n; ++i) {
                    int c = compare_values(va.elems[i], vb.elems[i]);
                    if (c != 0) return c;
                }
                return va.elems.size() < vb.elems.size() ? -1
                       : (va.elems.size() > vb.elems.size() ? 1 : 0);
            } else if constexpr (std::is_same_v<T, ValList>) {
                size_t n = std::min(va.elems.size(), vb.elems.size());
                for (size_t i = 0; i < n; ++i) {
                    int c = compare_values(va.elems[i], vb.elems[i]);
                    if (c != 0) return c;
                }
                return va.elems.size() < vb.elems.size() ? -1
                       : (va.elems.size() > vb.elems.size() ? 1 : 0);
            } else if constexpr (std::is_same_v<T, ValAdt>) {
                if (va.tag != vb.tag) return va.tag < vb.tag ? -1 : 1;
                size_t n = std::min(va.fields.size(), vb.fields.size());
                for (size_t i = 0; i < n; ++i) {
                    int c = compare_values(va.fields[i], vb.fields[i]);
                    if (c != 0) return c;
                }
                return va.fields.size() < vb.fields.size() ? -1
                       : (va.fields.size() > vb.fields.size() ? 1 : 0);
            } else {
                return 0;
            }
        },
        a.node);
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
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    if (std::holds_alternative<ValString>(n.elems[i].node)) {
                        s += "\"" + value_to_string(n.elems[i]) + "\"";
                    } else {
                        s += value_to_string(n.elems[i]);
                    }
                }
                s += ")";
                return s;
            } else if constexpr (std::is_same_v<T, ValList>) {
                std::string s = "[";
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    if (i > 0) s += ", ";
                    if (std::holds_alternative<ValString>(n.elems[i].node)) {
                        s += "\"" + value_to_string(n.elems[i]) + "\"";
                    } else {
                        s += value_to_string(n.elems[i]);
                    }
                }
                s += "]";
                return s;
            } else if constexpr (std::is_same_v<T, ValAdt>) {
                if (n.fields.empty()) return n.ctor_name;
                std::string s = n.ctor_name;
                for (const auto& f : n.fields) {
                    s += " ";
                    bool wrap = std::holds_alternative<ValAdt>(f.node) &&
                                !std::get<ValAdt>(f.node).fields.empty();
                    if (wrap) s += "(";
                    if (std::holds_alternative<ValString>(f.node)) {
                        s += "\"" + value_to_string(f) + "\"";
                    } else {
                        s += value_to_string(f);
                    }
                    if (wrap) s += ")";
                }
                return s;
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

}  // namespace cml
