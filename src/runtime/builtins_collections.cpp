#include "builtins_internal.hpp"
#include "cml/rc.hpp"
#include "cml/source.hpp"

#include <algorithm>

namespace cml::detail {

void append_collection_builtins(std::vector<BuiltinDef>& list) {
    auto tv = [](uint32_t id) { return make_tvar(id); };

    // rc_count : 'a -> Int
    list.push_back(BuiltinDef{
        "rc_count",
        TypeScheme{{0}, make_tfun(tv(0), int_type())},
        1,
        [](const std::vector<Value>& args) {
            return Value::make_int(static_cast<int64_t>(value_ref_count(args[0])));
        },
    });

    // rc_live_objects : Unit -> Int
    list.push_back(BuiltinDef{
        "rc_live_objects",
        TypeScheme{{}, make_tfun(unit_type(), int_type())},
        1,
        [](const std::vector<Value>&) {
            return Value::make_int(static_cast<int64_t>(RcStats::live_objects()));
        },
    });

    // heap_live_count : Unit -> Int
    list.push_back(BuiltinDef{
        "heap_live_count",
        TypeScheme{{}, make_tfun(unit_type(), int_type())},
        1,
        [](const std::vector<Value>&) {
            return Value::make_int(
                static_cast<int64_t>(live_heap_allocations().load(std::memory_order_relaxed)));
        },
    });

    // ptr_is_alive : *'a -> Bool
    list.push_back(BuiltinDef{
        "ptr_is_alive",
        TypeScheme{{0}, make_tfun(ptr_type(tv(0)), bool_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& p = std::get<ValPtr>(args[0].node);
            return Value::make_bool(p.ptr && p.ptr->alive);
        },
    });

    // Vector<'a> operations
    list.push_back(BuiltinDef{
        "vec_new",
        TypeScheme{{0}, make_tfun(unit_type(), vector_type(tv(0)))},
        1,
        [](const std::vector<Value>&) { return Value::make_vector(); },
    });

    list.push_back(BuiltinDef{
        "vec_push",
        TypeScheme{{0}, make_tfun(vector_type(tv(0)), make_tfun(tv(0), unit_type()))},
        2,
        [](const std::vector<Value>& args) {
            std::get<ValVector>(args[0].node).ptr->elems.push_back(args[1]);
            return Value::unit();
        },
    });

    list.push_back(BuiltinDef{
        "vec_pop",
        TypeScheme{{0}, make_tfun(vector_type(tv(0)), tv(0))},
        1,
        [](const std::vector<Value>& args) {
            auto& elems = std::get<ValVector>(args[0].node).ptr->elems;
            if (elems.empty()) {
                throw CompileError(Diagnostic{"runtime", "vec_pop called on empty Vector", {}});
            }
            Value back = std::move(elems.back());
            elems.pop_back();
            return back;
        },
    });

    list.push_back(BuiltinDef{
        "vec_get",
        TypeScheme{{0}, make_tfun(vector_type(tv(0)), make_tfun(int_type(), tv(0)))},
        2,
        [](const std::vector<Value>& args) {
            const auto& elems = std::get<ValVector>(args[0].node).ptr->elems;
            int64_t idx = std::get<ValInt>(args[1].node).value;
            if (idx < 0 || static_cast<size_t>(idx) >= elems.size()) {
                throw CompileError(Diagnostic{"runtime", "vec_get index out of bounds", {}});
            }
            return elems[static_cast<size_t>(idx)];
        },
    });

    list.push_back(BuiltinDef{
        "vec_set",
        TypeScheme{{0}, make_tfun(vector_type(tv(0)), make_tfun(int_type(), make_tfun(tv(0), unit_type())))},
        3,
        [](const std::vector<Value>& args) {
            auto& elems = std::get<ValVector>(args[0].node).ptr->elems;
            int64_t idx = std::get<ValInt>(args[1].node).value;
            if (idx < 0 || static_cast<size_t>(idx) >= elems.size()) {
                throw CompileError(Diagnostic{"runtime", "vec_set index out of bounds", {}});
            }
            elems[static_cast<size_t>(idx)] = args[2];
            return Value::unit();
        },
    });

    list.push_back(BuiltinDef{
        "vec_len",
        TypeScheme{{0}, make_tfun(vector_type(tv(0)), int_type())},
        1,
        [](const std::vector<Value>& args) {
            return Value::make_int(
                static_cast<int64_t>(std::get<ValVector>(args[0].node).ptr->elems.size()));
        },
    });

    list.push_back(BuiltinDef{
        "vec_to_list",
        TypeScheme{{0}, make_tfun(vector_type(tv(0)), list_type(tv(0)))},
        1,
        [](const std::vector<Value>& args) {
            return Value::make_list(std::get<ValVector>(args[0].node).ptr->elems);
        },
    });

    list.push_back(BuiltinDef{
        "vec_from_list",
        TypeScheme{{0}, make_tfun(list_type(tv(0)), vector_type(tv(0)))},
        1,
        [](const std::vector<Value>& args) {
            return Value::make_vector(std::get<ValList>(args[0].node).ptr->elems);
        },
    });

    // Dict<'k, 'v> operations
    list.push_back(BuiltinDef{
        "dict_new",
        TypeScheme{{0, 1}, make_tfun(unit_type(), dict_type(tv(0), tv(1)))},
        1,
        [](const std::vector<Value>&) { return Value::make_dict(); },
    });

    list.push_back(BuiltinDef{
        "dict_set",
        TypeScheme{{0, 1}, make_tfun(dict_type(tv(0), tv(1)),
                                    make_tfun(tv(0), make_tfun(tv(1), unit_type())))},
        3,
        [](const std::vector<Value>& args) {
            auto& entries = std::get<ValDict>(args[0].node).ptr->entries;
            for (auto& [k, v] : entries) {
                if (values_equal(k, args[1])) {
                    v = args[2];
                    return Value::unit();
                }
            }
            entries.emplace_back(args[1], args[2]);
            return Value::unit();
        },
    });

    list.push_back(BuiltinDef{
        "dict_get",
        TypeScheme{{0, 1}, make_tfun(dict_type(tv(0), tv(1)), make_tfun(tv(0), tv(1)))},
        2,
        [](const std::vector<Value>& args) {
            const auto& entries = std::get<ValDict>(args[0].node).ptr->entries;
            for (const auto& [k, v] : entries) {
                if (values_equal(k, args[1])) return v;
            }
            throw CompileError(Diagnostic{"runtime", "dict_get key not found: " + value_to_string(args[1]), {}});
        },
    });

    list.push_back(BuiltinDef{
        "dict_get_or",
        TypeScheme{{0, 1}, make_tfun(dict_type(tv(0), tv(1)),
                                    make_tfun(tv(0), make_tfun(tv(1), tv(1))))},
        3,
        [](const std::vector<Value>& args) {
            const auto& entries = std::get<ValDict>(args[0].node).ptr->entries;
            for (const auto& [k, v] : entries) {
                if (values_equal(k, args[1])) return v;
            }
            return args[2];
        },
    });

    list.push_back(BuiltinDef{
        "dict_has",
        TypeScheme{{0, 1}, make_tfun(dict_type(tv(0), tv(1)), make_tfun(tv(0), bool_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& entries = std::get<ValDict>(args[0].node).ptr->entries;
            for (const auto& [k, _] : entries) {
                if (values_equal(k, args[1])) return Value::make_bool(true);
            }
            return Value::make_bool(false);
        },
    });

    list.push_back(BuiltinDef{
        "dict_remove",
        TypeScheme{{0, 1}, make_tfun(dict_type(tv(0), tv(1)), make_tfun(tv(0), bool_type()))},
        2,
        [](const std::vector<Value>& args) {
            auto& entries = std::get<ValDict>(args[0].node).ptr->entries;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (values_equal(entries[i].first, args[1])) {
                    entries.erase(entries.begin() + static_cast<ptrdiff_t>(i));
                    return Value::make_bool(true);
                }
            }
            return Value::make_bool(false);
        },
    });

    list.push_back(BuiltinDef{
        "dict_keys",
        TypeScheme{{0, 1}, make_tfun(dict_type(tv(0), tv(1)), list_type(tv(0)))},
        1,
        [](const std::vector<Value>& args) {
            const auto& entries = std::get<ValDict>(args[0].node).ptr->entries;
            std::vector<Value> out;
            out.reserve(entries.size());
            for (const auto& [k, _] : entries) out.push_back(k);
            return Value::make_list(std::move(out));
        },
    });

    list.push_back(BuiltinDef{
        "dict_values",
        TypeScheme{{0, 1}, make_tfun(dict_type(tv(0), tv(1)), list_type(tv(1)))},
        1,
        [](const std::vector<Value>& args) {
            const auto& entries = std::get<ValDict>(args[0].node).ptr->entries;
            std::vector<Value> out;
            out.reserve(entries.size());
            for (const auto& [_, v] : entries) out.push_back(v);
            return Value::make_list(std::move(out));
        },
    });

    list.push_back(BuiltinDef{
        "dict_len",
        TypeScheme{{0, 1}, make_tfun(dict_type(tv(0), tv(1)), int_type())},
        1,
        [](const std::vector<Value>& args) {
            return Value::make_int(
                static_cast<int64_t>(std::get<ValDict>(args[0].node).ptr->entries.size()));
        },
    });

    // Set<'a> operations
    list.push_back(BuiltinDef{
        "set_new",
        TypeScheme{{0}, make_tfun(unit_type(), set_type(tv(0)))},
        1,
        [](const std::vector<Value>&) { return Value::make_set(); },
    });

    list.push_back(BuiltinDef{
        "set_add",
        TypeScheme{{0}, make_tfun(set_type(tv(0)), make_tfun(tv(0), bool_type()))},
        2,
        [](const std::vector<Value>& args) {
            auto& elems = std::get<ValSet>(args[0].node).ptr->elems;
            for (const auto& e : elems) {
                if (values_equal(e, args[1])) return Value::make_bool(false);
            }
            elems.push_back(args[1]);
            return Value::make_bool(true);
        },
    });

    list.push_back(BuiltinDef{
        "set_has",
        TypeScheme{{0}, make_tfun(set_type(tv(0)), make_tfun(tv(0), bool_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& elems = std::get<ValSet>(args[0].node).ptr->elems;
            for (const auto& e : elems) {
                if (values_equal(e, args[1])) return Value::make_bool(true);
            }
            return Value::make_bool(false);
        },
    });

    list.push_back(BuiltinDef{
        "set_remove",
        TypeScheme{{0}, make_tfun(set_type(tv(0)), make_tfun(tv(0), bool_type()))},
        2,
        [](const std::vector<Value>& args) {
            auto& elems = std::get<ValSet>(args[0].node).ptr->elems;
            for (size_t i = 0; i < elems.size(); ++i) {
                if (values_equal(elems[i], args[1])) {
                    elems.erase(elems.begin() + static_cast<ptrdiff_t>(i));
                    return Value::make_bool(true);
                }
            }
            return Value::make_bool(false);
        },
    });

    list.push_back(BuiltinDef{
        "set_len",
        TypeScheme{{0}, make_tfun(set_type(tv(0)), int_type())},
        1,
        [](const std::vector<Value>& args) {
            return Value::make_int(
                static_cast<int64_t>(std::get<ValSet>(args[0].node).ptr->elems.size()));
        },
    });

    list.push_back(BuiltinDef{
        "set_values",
        TypeScheme{{0}, make_tfun(set_type(tv(0)), list_type(tv(0)))},
        1,
        [](const std::vector<Value>& args) {
            return Value::make_list(std::get<ValSet>(args[0].node).ptr->elems);
        },
    });
}

}  // namespace cml::detail
