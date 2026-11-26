#pragma once

#include "cml/ast.hpp"
#include "cml/rc.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cml {

struct Value;
struct FunctionProto;
struct UpvalueObj;

struct RcTuple;
struct RcList;
struct RcAdt;
struct RcStruct;
struct RcVector;
struct RcDict;
struct RcSet;
struct HeapCell;

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
    Rc<RcTuple> ptr;
};

struct ValList {
    Rc<RcList> ptr;
};

struct ValAdt {
    Rc<RcAdt> ptr;
};

struct ValStruct {
    Rc<RcStruct> ptr;
};

struct ValVector {
    Rc<RcVector> ptr;
};

struct ValDict {
    Rc<RcDict> ptr;
};

struct ValSet {
    Rc<RcSet> ptr;
};

struct ValPtr {
    Rc<HeapCell> ptr;
};

struct ValClosure {
    std::shared_ptr<const FunctionProto> proto;
    std::vector<Rc<UpvalueObj>> upvalues;
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
    std::variant<ValUnit, ValInt, ValFloat, ValBool, ValString, ValTuple, ValList, ValAdt,
                 ValStruct, ValVector, ValDict, ValSet, ValPtr, ValClosure, ValNative, ValCtor,
                 ValAstExpr, ValAstPattern>
        node;

    static Value unit() { return Value{ValUnit{}}; }
    static Value make_int(int64_t v) { return Value{ValInt{v}}; }
    static Value make_float(double v) { return Value{ValFloat{v}}; }
    static Value make_bool(bool v) { return Value{ValBool{v}}; }
    static Value make_string(std::string v) { return Value{ValString{std::move(v)}}; }
    static Value make_tuple(std::vector<Value> elems);
    static Value make_list(std::vector<Value> elems);
    static Value make_adt(uint32_t tag, std::string name, std::vector<Value> fields = {});
    static Value make_struct(std::string type_name,
                             std::unordered_map<std::string, Value> fields = {});
    static Value make_vector(std::vector<Value> elems = {});
    static Value make_dict(std::vector<std::pair<Value, Value>> entries = {});
    static Value make_set(std::vector<Value> elems = {});
    static Value make_heap_ptr(Value val);
    static Value make_stack_ptr(size_t stack_slot);
    static Value make_global_ptr(std::string name);
    static Value make_ast_expr(ExprPtr e) { return Value{ValAstExpr{std::move(e)}}; }
    static Value make_ast_pattern(PatternPtr p) { return Value{ValAstPattern{std::move(p)}}; }
};

struct RcTuple : RcObject {
    std::vector<Value> elems;
    explicit RcTuple(std::vector<Value> e) : elems(std::move(e)) {}
};

struct RcList : RcObject {
    std::vector<Value> elems;
    explicit RcList(std::vector<Value> e) : elems(std::move(e)) {}
};

struct RcAdt : RcObject {
    uint32_t tag = 0;
    std::string ctor_name;
    std::vector<Value> fields;
    RcAdt(uint32_t t, std::string n, std::vector<Value> f)
        : tag(t), ctor_name(std::move(n)), fields(std::move(f)) {}
};

struct RcStruct : RcObject {
    std::string type_name;
    std::unordered_map<std::string, Value> fields;
    RcStruct(std::string tn, std::unordered_map<std::string, Value> f)
        : type_name(std::move(tn)), fields(std::move(f)) {}
};

struct RcVector : RcObject {
    std::vector<Value> elems;
    explicit RcVector(std::vector<Value> e = {}) : elems(std::move(e)) {}
};

struct RcDict : RcObject {
    std::vector<std::pair<Value, Value>> entries;
    explicit RcDict(std::vector<std::pair<Value, Value>> e = {}) : entries(std::move(e)) {}
};

struct RcSet : RcObject {
    std::vector<Value> elems;
    explicit RcSet(std::vector<Value> e = {}) : elems(std::move(e)) {}
};

struct UpvalueObj : RcObject {
    size_t stack_index = 0;
    Value closed;
    bool is_closed = false;
};

inline std::atomic<size_t>& live_heap_allocations() {
    static std::atomic<size_t> count{0};
    return count;
}

struct HeapCell : RcObject {
    Value value;
    bool alive = true;
    bool is_heap = true;
    bool is_stack_slot = false;
    size_t stack_slot = 0;
    bool is_global = false;
    std::string global_name;

    explicit HeapCell(Value v, bool heap = true)
        : value(std::move(v)), alive(true), is_heap(heap) {
        if (is_heap) {
            live_heap_allocations().fetch_add(1, std::memory_order_relaxed);
        }
    }

    explicit HeapCell(size_t slot)
        : value(Value::unit()), alive(true), is_heap(false), is_stack_slot(true), stack_slot(slot) {}

    HeapCell(std::string gname, bool /*global_tag*/)
        : value(Value::unit()), alive(true), is_heap(false), is_global(true), global_name(std::move(gname)) {}

    ~HeapCell() override {
        if (is_heap && alive) {
            live_heap_allocations().fetch_sub(1, std::memory_order_relaxed);
            alive = false;
        }
    }
};

inline Value Value::make_heap_ptr(Value val) {
    return Value{ValPtr{make_rc<HeapCell>(std::move(val), true)}};
}
inline Value Value::make_stack_ptr(size_t stack_slot) {
    return Value{ValPtr{make_rc<HeapCell>(stack_slot)}};
}
inline Value Value::make_global_ptr(std::string name) {
    return Value{ValPtr{make_rc<HeapCell>(std::move(name), true)}};
}

inline Value Value::make_tuple(std::vector<Value> elems) {
    return Value{ValTuple{make_rc<RcTuple>(std::move(elems))}};
}
inline Value Value::make_list(std::vector<Value> elems) {
    return Value{ValList{make_rc<RcList>(std::move(elems))}};
}
inline Value Value::make_adt(uint32_t tag, std::string name, std::vector<Value> fields) {
    return Value{ValAdt{make_rc<RcAdt>(tag, std::move(name), std::move(fields))}};
}
inline Value Value::make_struct(std::string type_name,
                                std::unordered_map<std::string, Value> fields) {
    return Value{ValStruct{make_rc<RcStruct>(std::move(type_name), std::move(fields))}};
}
inline Value Value::make_vector(std::vector<Value> elems) {
    return Value{ValVector{make_rc<RcVector>(std::move(elems))}};
}
inline Value Value::make_dict(std::vector<std::pair<Value, Value>> entries) {
    return Value{ValDict{make_rc<RcDict>(std::move(entries))}};
}
inline Value Value::make_set(std::vector<Value> elems) {
    return Value{ValSet{make_rc<RcSet>(std::move(elems))}};
}

bool values_equal(const Value& a, const Value& b);
int compare_values(const Value& a, const Value& b);
std::string value_to_string(const Value& v);
bool is_truthy(const Value& v);
size_t value_ref_count(const Value& v);

}  // namespace cml
