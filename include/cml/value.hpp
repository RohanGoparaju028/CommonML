#pragma once

#include "cml/ast.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace cml {

struct Value;
using ValuePtr = std::shared_ptr<const Value>;

struct FunctionProto;
struct UpvalueObj;

struct ValUnit {};

struct ValInt {
    int64_t value = 0;
};

struct ValFloat {
    double value = 0.0;
};

struct ValBool {
    bool value = false;
};

struct ValString {
    std::string value;
};

struct ValTuple {
    std::vector<Value> elems;
};

struct ValList {
    std::vector<Value> elems;
};

struct ValAdt {
    uint32_t tag = 0;
    std::string ctor_name;
    std::vector<Value> fields;
};

struct ValClosure {
    std::shared_ptr<const FunctionProto> proto;
    std::vector<std::shared_ptr<UpvalueObj>> upvalues;
};

using NativeFn = std::function<Value(const std::vector<Value>&)>;

struct ValNative {
    std::string name;
    size_t arity = 1;
    std::vector<Value> bound_args;
    NativeFn fn;
};

struct ValCtor {
    uint32_t tag = 0;
    std::string name;
    size_t arity = 0;
    std::vector<Value> bound_args;
};

struct ValAstExpr {
    ExprPtr expr;
};

struct ValAstPattern {
    PatternPtr pat;
};

struct Value {
    std::variant<ValUnit, ValInt, ValFloat, ValBool, ValString, ValTuple, ValList,
                 ValAdt, ValClosure, ValNative, ValCtor, ValAstExpr, ValAstPattern>
        node;

    static Value unit() { return Value{ValUnit{}}; }
    static Value make_int(int64_t v) { return Value{ValInt{v}}; }
    static Value make_float(double v) { return Value{ValFloat{v}}; }
    static Value make_bool(bool v) { return Value{ValBool{v}}; }
    static Value make_string(std::string v) { return Value{ValString{std::move(v)}}; }
    static Value make_tuple(std::vector<Value> elems) { return Value{ValTuple{std::move(elems)}}; }
    static Value make_list(std::vector<Value> elems) { return Value{ValList{std::move(elems)}}; }
    static Value make_adt(uint32_t tag, std::string name, std::vector<Value> fields = {}) {
        return Value{ValAdt{tag, std::move(name), std::move(fields)}};
    }
    static Value make_ast_expr(ExprPtr e) { return Value{ValAstExpr{std::move(e)}}; }
    static Value make_ast_pattern(PatternPtr p) { return Value{ValAstPattern{std::move(p)}}; }
};

bool values_equal(const Value& a, const Value& b);
int compare_values(const Value& a, const Value& b);
std::string value_to_string(const Value& v);
bool is_truthy(const Value& v);

}  // namespace cml
