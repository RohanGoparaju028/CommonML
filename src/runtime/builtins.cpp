#include "cml/builtins.hpp"
#include "cml/source.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace cml {

const std::vector<BuiltinDef>& get_builtins() {
    static const std::vector<BuiltinDef> builtins = []() {
        std::vector<BuiltinDef> list;
        auto tv = [](uint32_t id) { return make_tvar(id); };

        // print : 'a -> Unit
        list.push_back(BuiltinDef{
            "print",
            TypeScheme{{0}, make_tfun(tv(0), unit_type())},
            1,
            [](const std::vector<Value>& args) {
                std::cout << value_to_string(args[0]);
                return Value::unit();
            },
        });

        // println : 'a -> Unit
        list.push_back(BuiltinDef{
            "println",
            TypeScheme{{0}, make_tfun(tv(0), unit_type())},
            1,
            [](const std::vector<Value>& args) {
                std::cout << value_to_string(args[0]) << "\n";
                return Value::unit();
            },
        });

        // to_string : 'a -> String
        list.push_back(BuiltinDef{
            "to_string",
            TypeScheme{{0}, make_tfun(tv(0), string_type())},
            1,
            [](const std::vector<Value>& args) {
                return Value::make_string(value_to_string(args[0]));
            },
        });

        // not : Bool -> Bool
        list.push_back(BuiltinDef{
            "not",
            TypeScheme{{}, make_tfun(bool_type(), bool_type())},
            1,
            [](const std::vector<Value>& args) {
                return Value::make_bool(!std::get<ValBool>(args[0].node).value);
            },
        });

        // abs : Int -> Int
        list.push_back(BuiltinDef{
            "abs",
            TypeScheme{{}, make_tfun(int_type(), int_type())},
            1,
            [](const std::vector<Value>& args) {
                int64_t x = std::get<ValInt>(args[0].node).value;
                return Value::make_int(x < 0 ? -x : x);
            },
        });

        // int_of_float : Float -> Int
        list.push_back(BuiltinDef{
            "int_of_float",
            TypeScheme{{}, make_tfun(float_type(), int_type())},
            1,
            [](const std::vector<Value>& args) {
                return Value::make_int(
                    static_cast<int64_t>(std::get<ValFloat>(args[0].node).value));
            },
        });

        // float_of_int : Int -> Float
        list.push_back(BuiltinDef{
            "float_of_int",
            TypeScheme{{}, make_tfun(int_type(), float_type())},
            1,
            [](const std::vector<Value>& args) {
                return Value::make_float(
                    static_cast<double>(std::get<ValInt>(args[0].node).value));
            },
        });

        // str_len : String -> Int
        list.push_back(BuiltinDef{
            "str_len",
            TypeScheme{{}, make_tfun(string_type(), int_type())},
            1,
            [](const std::vector<Value>& args) {
                return Value::make_int(
                    static_cast<int64_t>(std::get<ValString>(args[0].node).value.size()));
            },
        });

        // str_sub : String -> Int -> Int -> String
        list.push_back(BuiltinDef{
            "str_sub",
            TypeScheme{{}, make_tfun(string_type(), make_tfun(int_type(), make_tfun(int_type(), string_type())))},
            3,
            [](const std::vector<Value>& args) {
                const auto& s = std::get<ValString>(args[0].node).value;
                int64_t start = std::get<ValInt>(args[1].node).value;
                int64_t len = std::get<ValInt>(args[2].node).value;
                if (start < 0 || len < 0 || static_cast<size_t>(start) > s.size()) {
                    throw CompileError(Diagnostic{"runtime", "str_sub index out of bounds", {}});
                }
                return Value::make_string(
                    s.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
            },
        });

        // list_len : List 'a -> Int
        list.push_back(BuiltinDef{
            "list_len",
            TypeScheme{{0}, make_tfun(list_type(tv(0)), int_type())},
            1,
            [](const std::vector<Value>& args) {
                return Value::make_int(
                    static_cast<int64_t>(std::get<ValList>(args[0].node).elems.size()));
            },
        });

        // is_empty : List 'a -> Bool
        list.push_back(BuiltinDef{
            "is_empty",
            TypeScheme{{0}, make_tfun(list_type(tv(0)), bool_type())},
            1,
            [](const std::vector<Value>& args) {
                return Value::make_bool(std::get<ValList>(args[0].node).elems.empty());
            },
        });

        // head : List 'a -> 'a
        list.push_back(BuiltinDef{
            "head",
            TypeScheme{{0}, make_tfun(list_type(tv(0)), tv(0))},
            1,
            [](const std::vector<Value>& args) {
                const auto& elems = std::get<ValList>(args[0].node).elems;
                if (elems.empty()) {
                    throw CompileError(Diagnostic{"runtime", "head called on empty list", {}});
                }
                return elems.front();
            },
        });

        // tail : List 'a -> List 'a
        list.push_back(BuiltinDef{
            "tail",
            TypeScheme{{0}, make_tfun(list_type(tv(0)), list_type(tv(0)))},
            1,
            [](const std::vector<Value>& args) {
                const auto& elems = std::get<ValList>(args[0].node).elems;
                if (elems.empty()) {
                    throw CompileError(Diagnostic{"runtime", "tail called on empty list", {}});
                }
                return Value::make_list(std::vector<Value>(elems.begin() + 1, elems.end()));
            },
        });

        // panic : String -> 'a
        list.push_back(BuiltinDef{
            "panic",
            TypeScheme{{0}, make_tfun(string_type(), tv(0))},
            1,
            [](const std::vector<Value>& args) -> Value {
                throw CompileError(Diagnostic{
                    "runtime", "panic: " + std::get<ValString>(args[0].node).value, {}});
            },
        });

        return list;
    }();
    return builtins;
}

}  // namespace cml
