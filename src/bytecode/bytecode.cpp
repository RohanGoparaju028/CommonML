#include "cml/bytecode.hpp"

#include <iomanip>
#include <sstream>
#include <unordered_map>

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
        case OpCode::BitNot: return "BIT_NOT";
        case OpCode::LogicalNot: return "LOGICAL_NOT";
        case OpCode::AddInt: return "ADD_INT";
        case OpCode::SubInt: return "SUB_INT";
        case OpCode::MulInt: return "MUL_INT";
        case OpCode::DivInt: return "DIV_INT";
        case OpCode::ModInt: return "MOD_INT";
        case OpCode::AddFloat: return "ADD_FLOAT";
        case OpCode::SubFloat: return "SUB_FLOAT";
        case OpCode::MulFloat: return "MUL_FLOAT";
        case OpCode::DivFloat: return "DIV_FLOAT";
        case OpCode::BitAnd: return "BIT_AND";
        case OpCode::BitOr: return "BIT_OR";
        case OpCode::BitXor: return "BIT_XOR";
        case OpCode::Shl: return "SHL";
        case OpCode::Shr: return "SHR";
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
        case OpCode::MakeStruct: return "MAKE_STRUCT";
        case OpCode::GetField: return "GET_FIELD";
        case OpCode::SetField: return "SET_FIELD";
        case OpCode::NewHeap: return "NEW_HEAP";
        case OpCode::CloseHeap: return "CLOSE_HEAP";
        case OpCode::DerefPtr: return "DEREF_PTR";
        case OpCode::SetDeref: return "SET_DEREF";
        case OpCode::GetPtrField: return "GET_PTR_FIELD";
        case OpCode::SetPtrField: return "SET_PTR_FIELD";
        case OpCode::AddrOfLocal: return "ADDR_OF_LOCAL";
        case OpCode::AddrOfGlobal: return "ADDR_OF_GLOBAL";
        case OpCode::IterItems: return "ITER_ITEMS";
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

OpCode opcode_from_name(const std::string& name) {
    static const std::unordered_map<std::string, OpCode> table = {
        {"CONST", OpCode::Const},
        {"POP", OpCode::Pop},
        {"DUP", OpCode::Dup},
        {"GET_LOCAL", OpCode::GetLocal},
        {"SET_LOCAL", OpCode::SetLocal},
        {"GET_UPVALUE", OpCode::GetUpvalue},
        {"SET_UPVALUE", OpCode::SetUpvalue},
        {"CLOSE_UPVALUE", OpCode::CloseUpvalue},
        {"GET_GLOBAL", OpCode::GetGlobal},
        {"DEF_GLOBAL", OpCode::DefGlobal},
        {"NEG_INT", OpCode::NegInt},
        {"NEG_FLOAT", OpCode::NegFloat},
        {"BIT_NOT", OpCode::BitNot},
        {"LOGICAL_NOT", OpCode::LogicalNot},
        {"ADD_INT", OpCode::AddInt},
        {"SUB_INT", OpCode::SubInt},
        {"MUL_INT", OpCode::MulInt},
        {"DIV_INT", OpCode::DivInt},
        {"MOD_INT", OpCode::ModInt},
        {"ADD_FLOAT", OpCode::AddFloat},
        {"SUB_FLOAT", OpCode::SubFloat},
        {"MUL_FLOAT", OpCode::MulFloat},
        {"DIV_FLOAT", OpCode::DivFloat},
        {"BIT_AND", OpCode::BitAnd},
        {"BIT_OR", OpCode::BitOr},
        {"BIT_XOR", OpCode::BitXor},
        {"SHL", OpCode::Shl},
        {"SHR", OpCode::Shr},
        {"CONCAT_STR", OpCode::ConcatStr},
        {"CONS", OpCode::Cons},
        {"EQ", OpCode::Eq},
        {"NEQ", OpCode::Neq},
        {"LT", OpCode::Lt},
        {"LE", OpCode::Le},
        {"GT", OpCode::Gt},
        {"GE", OpCode::Ge},
        {"MAKE_TUPLE", OpCode::MakeTuple},
        {"MAKE_LIST", OpCode::MakeList},
        {"MAKE_ADT", OpCode::MakeAdt},
        {"MAKE_STRUCT", OpCode::MakeStruct},
        {"GET_FIELD", OpCode::GetField},
        {"SET_FIELD", OpCode::SetField},
        {"NEW_HEAP", OpCode::NewHeap},
        {"CLOSE_HEAP", OpCode::CloseHeap},
        {"DEREF_PTR", OpCode::DerefPtr},
        {"SET_DEREF", OpCode::SetDeref},
        {"GET_PTR_FIELD", OpCode::GetPtrField},
        {"SET_PTR_FIELD", OpCode::SetPtrField},
        {"ADDR_OF_LOCAL", OpCode::AddrOfLocal},
        {"ADDR_OF_GLOBAL", OpCode::AddrOfGlobal},
        {"ITER_ITEMS", OpCode::IterItems},
        {"MAKE_CLOSURE", OpCode::MakeClosure},
        {"MAKE_QUOTE", OpCode::MakeQuote},
        {"JUMP", OpCode::Jump},
        {"JUMP_IF_FALSE", OpCode::JumpIfFalse},
        {"JUMP_IF_TRUE", OpCode::JumpIfTrue},
        {"CALL", OpCode::Call},
        {"TAIL_CALL", OpCode::TailCall},
        {"RETURN", OpCode::Return},
        {"MATCH_BOOL", OpCode::MatchBool},
        {"MATCH_INT", OpCode::MatchInt},
        {"MATCH_FLOAT", OpCode::MatchFloat},
        {"MATCH_STRING", OpCode::MatchString},
        {"MATCH_UNIT", OpCode::MatchUnit},
        {"MATCH_TUPLE", OpCode::MatchTuple},
        {"MATCH_LIST_LEN", OpCode::MatchListLen},
        {"MATCH_CONS", OpCode::MatchCons},
        {"MATCH_ADT", OpCode::MatchAdt},
        {"TUPLE_GET", OpCode::TupleGet},
        {"LIST_GET", OpCode::ListGet},
        {"LIST_TAIL", OpCode::ListTail},
        {"ADT_FIELD", OpCode::AdtField},
        {"MATCH_FAIL", OpCode::MatchFail},
    };
    auto it = table.find(name);
    if (it == table.end()) {
        throw CompileError(Diagnostic{"bytecode", "unknown opcode '" + name + "'", {}});
    }
    return it->second;
}

static int collect_protos(const std::shared_ptr<const FunctionProto>& proto,
                          std::vector<std::shared_ptr<const FunctionProto>>& ordered,
                          std::unordered_map<const FunctionProto*, int>& ids) {
    auto it = ids.find(proto.get());
    if (it != ids.end()) return it->second;
    for (const auto& c : proto->chunk.constants) {
        if (auto* cl = std::get_if<ValClosure>(&c.node)) {
            if (cl->proto) collect_protos(cl->proto, ordered, ids);
        }
    }
    int id = static_cast<int>(ordered.size());
    ordered.push_back(proto);
    ids[proto.get()] = id;
    return id;
}

static void write_len_str(std::ostringstream& os, const std::string& s) {
    os << s.size() << ":" << s << "\n";
}

std::string serialize_bytecode(const BytecodeProgram& prog) {
    std::ostringstream os;
    os << "CMLBC 1\n";
    for (const auto& c : prog.ctors) {
        os << "CTOR " << c.name << " " << c.tag << " " << c.arity << "\n";
    }
    std::vector<std::shared_ptr<const FunctionProto>> ordered;
    std::unordered_map<const FunctionProto*, int> ids;
    int entry_id = collect_protos(prog.entry, ordered, ids);

    for (size_t id = 0; id < ordered.size(); ++id) {
        const auto& p = ordered[id];
        os << "PROTO " << id << " " << p->arity << " " << p->num_locals << " ";
        write_len_str(os, p->name);
        for (const auto& uv : p->upvalues) {
            os << "UPVAL " << (uv.is_local ? 1 : 0) << " " << uv.index << "\n";
        }
        for (const auto& c : p->chunk.constants) {
            if (std::holds_alternative<ValUnit>(c.node)) {
                os << "CONST U\n";
            } else if (auto* iv = std::get_if<ValInt>(&c.node)) {
                os << "CONST I " << iv->value << "\n";
            } else if (auto* fv = std::get_if<ValFloat>(&c.node)) {
                os << "CONST F " << std::setprecision(17) << fv->value << "\n";
            } else if (auto* bv = std::get_if<ValBool>(&c.node)) {
                os << "CONST B " << (bv->value ? 1 : 0) << "\n";
            } else if (auto* sv = std::get_if<ValString>(&c.node)) {
                os << "CONST S ";
                write_len_str(os, sv->value);
            } else if (auto* cl = std::get_if<ValClosure>(&c.node)) {
                os << "CONST P " << ids[cl->proto.get()] << "\n";
            } else {
                throw CompileError(Diagnostic{"bytecode", "unsupported constant in bytecode serialization", {}});
            }
        }
        for (const auto& nm : p->chunk.names) {
            os << "NAME ";
            write_len_str(os, nm);
        }
        for (const auto& ins : p->chunk.code) {
            os << "CODE " << opcode_name(ins.op) << " " << ins.arg1 << " " << ins.arg2 << "\n";
        }
        os << "ENDPROTO\n";
    }
    os << "ENTRY " << entry_id << "\n";
    return os.str();
}

BytecodeProgram deserialize_bytecode(const std::string& data) {
    size_t pos = 0;
    if (data.rfind("CMLBC", 0) != 0) {
        size_t marker = data.find("\nCMLBC ");
        if (marker != std::string::npos) {
            pos = marker + 1;
        }
    }
    auto read_word = [&]() -> std::string {
        while (pos < data.size() && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\r' || data[pos] == '\n')) {
            ++pos;
        }
        size_t start = pos;
        while (pos < data.size() && data[pos] != ' ' && data[pos] != '\t' && data[pos] != '\r' && data[pos] != '\n') {
            ++pos;
        }
        return data.substr(start, pos - start);
    };
    auto read_len_str = [&]() -> std::string {
        while (pos < data.size() && (data[pos] == ' ' || data[pos] == '\t')) ++pos;
        size_t colon = data.find(':', pos);
        if (colon == std::string::npos) {
            throw CompileError(Diagnostic{"bytecode", "malformed length-prefixed string", {}});
        }
        size_t len = static_cast<size_t>(std::stoull(data.substr(pos, colon - pos)));
        pos = colon + 1;
        if (pos + len > data.size()) {
            throw CompileError(Diagnostic{"bytecode", "truncated string in bytecode", {}});
        }
        std::string out = data.substr(pos, len);
        pos += len;
        return out;
    };

    if (read_word() != "CMLBC" || read_word() != "1") {
        throw CompileError(Diagnostic{"bytecode", "invalid CMLBC header", {}});
    }

    BytecodeProgram prog;
    std::unordered_map<int, std::shared_ptr<FunctionProto>> protos;
    int entry_id = -1;

    while (true) {
        std::string tag = read_word();
        if (tag.empty()) break;
        if (tag == "CTOR") {
            BytecodeCtor c;
            c.name = read_word();
            c.tag = static_cast<uint32_t>(std::stoul(read_word()));
            c.arity = static_cast<size_t>(std::stoull(read_word()));
            prog.ctors.push_back(std::move(c));
        } else if (tag == "PROTO") {
            int id = std::stoi(read_word());
            auto proto = std::make_shared<FunctionProto>();
            proto->arity = static_cast<size_t>(std::stoull(read_word()));
            proto->num_locals = static_cast<size_t>(std::stoull(read_word()));
            proto->name = read_len_str();

            while (true) {
                std::string sub = read_word();
                if (sub == "ENDPROTO") break;
                if (sub == "UPVAL") {
                    bool is_loc = std::stoi(read_word()) != 0;
                    uint32_t idx = static_cast<uint32_t>(std::stoul(read_word()));
                    proto->upvalues.push_back(UpvalueDesc{is_loc, idx});
                } else if (sub == "CONST") {
                    std::string kind = read_word();
                    if (kind == "U") {
                        proto->chunk.add_constant(Value::unit());
                    } else if (kind == "I") {
                        proto->chunk.add_constant(Value::make_int(std::stoll(read_word())));
                    } else if (kind == "F") {
                        proto->chunk.add_constant(Value::make_float(std::stod(read_word())));
                    } else if (kind == "B") {
                        proto->chunk.add_constant(Value::make_bool(std::stoi(read_word()) != 0));
                    } else if (kind == "S") {
                        proto->chunk.add_constant(Value::make_string(read_len_str()));
                    } else if (kind == "P") {
                        int pid = std::stoi(read_word());
                        auto it = protos.find(pid);
                        if (it == protos.end()) {
                            throw CompileError(Diagnostic{"bytecode", "missing proto id in CONST P", {}});
                        }
                        proto->chunk.add_constant(Value{ValClosure{it->second, {}}});
                    } else {
                        throw CompileError(Diagnostic{"bytecode", "unknown constant kind '" + kind + "'", {}});
                    }
                } else if (sub == "NAME") {
                    proto->chunk.add_name(read_len_str());
                } else if (sub == "CODE") {
                    OpCode op = opcode_from_name(read_word());
                    int32_t a1 = static_cast<int32_t>(std::stol(read_word()));
                    int32_t a2 = static_cast<int32_t>(std::stol(read_word()));
                    proto->chunk.emit(op, a1, a2);
                } else {
                    throw CompileError(Diagnostic{"bytecode", "unexpected directive '" + sub + "' in PROTO", {}});
                }
            }
            protos[id] = proto;
        } else if (tag == "ENTRY") {
            entry_id = std::stoi(read_word());
        } else {
            throw CompileError(Diagnostic{"bytecode", "unexpected top-level directive '" + tag + "'", {}});
        }
    }

    auto it = protos.find(entry_id);
    if (it == protos.end()) {
        throw CompileError(Diagnostic{"bytecode", "missing ENTRY proto in CMLBC", {}});
    }
    prog.entry = it->second;
    return prog;
}

}  // namespace cml
