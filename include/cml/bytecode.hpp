#pragma once

#include "cml/ast.hpp"
#include "cml/source.hpp"
#include "cml/value.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cml {

enum class OpCode : uint8_t {
    Const,
    Pop,
    Dup,

    GetLocal,
    SetLocal,
    GetUpvalue,
    SetUpvalue,
    CloseUpvalue,
    GetGlobal,
    DefGlobal,

    NegInt,
    NegFloat,
    BitNot,
    LogicalNot,
    AddInt,
    SubInt,
    MulInt,
    DivInt,
    ModInt,
    AddFloat,
    SubFloat,
    MulFloat,
    DivFloat,
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr,
    ConcatStr,
    Cons,

    Eq,
    Neq,
    Lt,
    Le,
    Gt,
    Ge,

    MakeTuple,
    MakeList,
    MakeAdt,
    MakeStruct,
    GetField,
    SetField,
    NewHeap,
    CloseHeap,
    DerefPtr,
    SetDeref,
    GetPtrField,
    SetPtrField,
    AddrOfLocal,
    AddrOfGlobal,
    IterItems,
    MakeClosure,
    MakeQuote,

    Jump,
    JumpIfFalse,
    JumpIfTrue,

    Call,
    TailCall,
    Return,

    // Pattern-matching instructions
    MatchBool,
    MatchInt,
    MatchFloat,
    MatchString,
    MatchUnit,
    MatchTuple,
    MatchListLen,
    MatchCons,
    MatchAdt,
    TupleGet,
    ListGet,
    ListTail,
    AdtField,

    MatchFail,
};

struct Instruction {
    OpCode op = OpCode::Pop;
    int32_t arg1 = 0;
    int32_t arg2 = 0;
    SourceSpan span;
};

struct UpvalueDesc {
    bool is_local = true;
    uint32_t index = 0;
};

struct QuoteTemplate {
    ExprPtr expr;
    size_t slot_count = 0;
};

struct Chunk {
    std::vector<Instruction> code;
    std::vector<Value> constants;
    std::vector<std::string> names;
    std::vector<QuoteTemplate> quotes;

    uint32_t add_constant(Value v);
    uint32_t add_name(const std::string& name);
    uint32_t add_quote(ExprPtr expr, size_t slot_count);
    size_t emit(OpCode op, int32_t arg1 = 0, int32_t arg2 = 0, const SourceSpan& span = {});
    void patch_jump(size_t instr_idx, int32_t target);
};

struct FunctionProto {
    std::string name;
    size_t arity = 1;
    size_t num_locals = 0;
    Chunk chunk;
    std::vector<UpvalueDesc> upvalues;
};

const char* opcode_name(OpCode op);
OpCode opcode_from_name(const std::string& name);
std::string disassemble_chunk(const Chunk& chunk, const std::string& title = "<script>");

struct BytecodeCtor {
    std::string name;
    uint32_t tag = 0;
    size_t arity = 0;
};

struct BytecodeProgram {
    std::vector<BytecodeCtor> ctors;
    std::shared_ptr<FunctionProto> entry;
};

std::string serialize_bytecode(const BytecodeProgram& prog);
BytecodeProgram deserialize_bytecode(const std::string& data);

}  // namespace cml
