#include "cml/bytecode.hpp"

#include <iomanip>
#include <sstream>

namespace cml {

uint32_t Chunk::add_constant(Value v) {
    constants.push_back(std::move(v));
    return static_cast<uint32_t>(constants.size() - 1);
}

uint32_t Chunk::add_name(const std::string& name) {
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            return static_cast<uint32_t>(i);
        }
    }
    names.push_back(name);
    return static_cast<uint32_t>(names.size() - 1);
}

uint32_t Chunk::add_quote(ExprPtr expr, size_t slot_count) {
    quotes.push_back(QuoteTemplate{std::move(expr), slot_count});
    return static_cast<uint32_t>(quotes.size() - 1);
}

size_t Chunk::emit(OpCode op, int32_t arg1, int32_t arg2, const SourceSpan& span) {
    code.push_back(Instruction{op, arg1, arg2, span});
    return code.size() - 1;
}

void Chunk::patch_jump(size_t instr_idx, int32_t target) {
    Instruction& ins = code[instr_idx];
    switch (ins.op) {
        case OpCode::Jump:
        case OpCode::JumpIfFalse:
        case OpCode::JumpIfTrue:
            ins.arg1 = target;
            break;
        case OpCode::MatchBool:
        case OpCode::MatchInt:
        case OpCode::MatchFloat:
        case OpCode::MatchString:
        case OpCode::MatchUnit:
        case OpCode::MatchTuple:
        case OpCode::MatchListLen:
        case OpCode::MatchCons:
        case OpCode::MatchAdt:
            ins.arg2 = target;
            break;
        default:
            ins.arg1 = target;
            break;
    }
}

const char* opcode_name(OpCode op) {
    switch (op) {
        case OpCode::Const: return "CONST";
        case OpCode::Pop: return "POP";
        case OpCode::Dup: return "DUP";
        case OpCode::GetLocal: return "GET_LOCAL";
        case OpCode::SetLocal: return "SET_LOCAL";
        case OpCode::GetUpvalue: return "GET_UPVALUE";
        case OpCode::SetUpvalue: return "SET_UPVALUE";
        case OpCode::CloseUpvalue: return "CLOSE_UPVALUE";
        case OpCode::GetGlobal: return "GET_GLOBAL";
        case OpCode::DefGlobal: return "DEF_GLOBAL";
        case OpCode::NegInt: return "NEG_INT";
        case OpCode::NegFloat: return "NEG_FLOAT";
        case OpCode::AddInt: return "ADD_INT";
        case OpCode::SubInt: return "SUB_INT";
        case OpCode::MulInt: return "MUL_INT";
        case OpCode::DivInt: return "DIV_INT";
        case OpCode::ModInt: return "MOD_INT";
        case OpCode::AddFloat: return "ADD_FLOAT";
        case OpCode::SubFloat: return "SUB_FLOAT";
        case OpCode::MulFloat: return "MUL_FLOAT";
        case OpCode::DivFloat: return "DIV_FLOAT";
        case OpCode::ConcatStr: return "CONCAT_STR";
        case OpCode::Cons: return "CONS";
        case OpCode::Eq: return "EQ";
        case OpCode::Neq: return "NEQ";
        case OpCode::Lt: return "LT";
        case OpCode::Le: return "LE";
        case OpCode::Gt: return "GT";
        case OpCode::Ge: return "GE";
        case OpCode::MakeTuple: return "MAKE_TUPLE";
        case OpCode::MakeList: return "MAKE_LIST";
        case OpCode::MakeAdt: return "MAKE_ADT";
        case OpCode::MakeClosure: return "MAKE_CLOSURE";
        case OpCode::MakeQuote: return "MAKE_QUOTE";
        case OpCode::Jump: return "JUMP";
        case OpCode::JumpIfFalse: return "JUMP_IF_FALSE";
        case OpCode::JumpIfTrue: return "JUMP_IF_TRUE";
        case OpCode::Call: return "CALL";
        case OpCode::TailCall: return "TAIL_CALL";
        case OpCode::Return: return "RETURN";
        case OpCode::MatchBool: return "MATCH_BOOL";
        case OpCode::MatchInt: return "MATCH_INT";
        case OpCode::MatchFloat: return "MATCH_FLOAT";
        case OpCode::MatchString: return "MATCH_STRING";
        case OpCode::MatchUnit: return "MATCH_UNIT";
        case OpCode::MatchTuple: return "MATCH_TUPLE";
        case OpCode::MatchListLen: return "MATCH_LIST_LEN";
        case OpCode::MatchCons: return "MATCH_CONS";
        case OpCode::MatchAdt: return "MATCH_ADT";
        case OpCode::TupleGet: return "TUPLE_GET";
        case OpCode::ListGet: return "LIST_GET";
        case OpCode::ListTail: return "LIST_TAIL";
        case OpCode::AdtField: return "ADT_FIELD";
        case OpCode::MatchFail: return "MATCH_FAIL";
    }
    return "UNKNOWN";
}

std::string disassemble_chunk(const Chunk& chunk, const std::string& title) {
    std::ostringstream out;
    out << "== " << title << " ==\n";
    for (size_t i = 0; i < chunk.code.size(); ++i) {
        const auto& ins = chunk.code[i];
        out << std::setw(4) << std::setfill('0') << i << "  "
            << std::left << std::setw(15) << std::setfill(' ') << opcode_name(ins.op);
        switch (ins.op) {
            case OpCode::Const:
            case OpCode::MakeClosure:
                out << ins.arg1 << " (" << value_to_string(chunk.constants[ins.arg1]) << ")";
                break;
            case OpCode::GetGlobal:
            case OpCode::DefGlobal:
                out << ins.arg1 << " (" << chunk.names[ins.arg1] << ")";
                break;
            default:
                if (ins.arg1 != 0 || ins.arg2 != 0) {
                    out << ins.arg1;
                    if (ins.arg2 != 0) out << ", " << ins.arg2;
                }
                break;
        }
        out << "\n";
    }
    for (const auto& c : chunk.constants) {
        if (auto* cl = std::get_if<ValClosure>(&c.node)) {
            if (cl->proto) {
                out << "\n" << disassemble_chunk(cl->proto->chunk, cl->proto->name);
            }
        }
    }
    return out.str();
}

}  // namespace cml
