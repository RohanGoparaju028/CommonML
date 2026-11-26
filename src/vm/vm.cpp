#include "cml/vm.hpp"
#include "cml/builtins.hpp"

#include <algorithm>

namespace cml {

VM::VM() {
    register_builtins();
}

void VM::register_builtins() {
    for (const auto& b : get_builtins()) {
        globals_[b.name] = Value{ValNative{b.name, b.arity, {}, b.fn}};
    }
}

void VM::register_ctors(const std::unordered_map<std::string, CtorInfo>& ctors) {
    for (const auto& [name, info] : ctors) {
        if (info.arity == 0) {
            globals_[name] = Value::make_adt(info.tag, name, {});
        } else {
            globals_[name] = Value{ValCtor{info.tag, name, info.arity, {}}};
        }
    }
}

void VM::set_global(const std::string& name, Value val) {
    globals_[name] = std::move(val);
}

const Value* VM::get_global(const std::string& name) const {
    auto it = globals_.find(name);
    return it == globals_.end() ? nullptr : &it->second;
}

void VM::runtime_error(const std::string& msg, const SourceSpan& span) const {
    throw CompileError(Diagnostic{"runtime", msg, span});
}

Rc<UpvalueObj> VM::capture_upvalue(size_t stack_slot) {
    for (const auto& uv : open_upvalues_) {
        if (!uv->is_closed && uv->stack_index == stack_slot) {
            return uv;
        }
    }
    auto uv = make_rc<UpvalueObj>();
    uv->stack_index = stack_slot;
    open_upvalues_.push_back(uv);
    return uv;
}

void VM::close_upvalues(size_t from_slot) {
    auto it = open_upvalues_.begin();
    while (it != open_upvalues_.end()) {
        auto& uv = *it;
        if (!uv->is_closed && uv->stack_index >= from_slot) {
            uv->closed = stack_[uv->stack_index];
            uv->is_closed = true;
            it = open_upvalues_.erase(it);
        } else {
            ++it;
        }
    }
    invalidate_stack_ptrs(from_slot);
}

void VM::invalidate_stack_ptrs(size_t from_slot) {
    auto it = open_stack_ptrs_.begin();
    while (it != open_stack_ptrs_.end()) {
        auto& cell = *it;
        if (cell->stack_slot >= from_slot) {
            cell->alive = false;
            cell->is_stack_slot = false;
            it = open_stack_ptrs_.erase(it);
        } else {
            ++it;
        }
    }
}

const Value& VM::read_ptr_cell(const Rc<HeapCell>& cell, const SourceSpan& span) const {
    if (!cell || !cell->alive) {
        runtime_error("use after free or dangling pointer dereference", span);
    }
    if (cell->is_stack_slot) {
        if (cell->stack_slot >= stack_.size()) {
            runtime_error("dangling stack pointer dereference", span);
        }
        return stack_[cell->stack_slot];
    }
    if (cell->is_global) {
        auto it = globals_.find(cell->global_name);
        if (it == globals_.end()) {
            runtime_error("dangling global pointer '" + cell->global_name + "'", span);
        }
        return it->second;
    }
    return cell->value;
}

void VM::write_ptr_cell(const Rc<HeapCell>& cell, Value val, const SourceSpan& span) {
    if (!cell || !cell->alive) {
        runtime_error("use after free or dangling pointer write", span);
    }
    if (cell->is_stack_slot) {
        if (cell->stack_slot >= stack_.size()) {
            runtime_error("dangling stack pointer write", span);
        }
        stack_[cell->stack_slot] = std::move(val);
        return;
    }
    if (cell->is_global) {
        globals_[cell->global_name] = std::move(val);
        return;
    }
    cell->value = std::move(val);
}

Value VM::run(const std::shared_ptr<FunctionProto>& entry) {
    stack_.clear();
    frames_.clear();
    open_upvalues_.clear();
    open_stack_ptrs_.clear();

    ValClosure top{entry, {}};
    stack_.push_back(Value{top});
    size_t base = stack_.size();
    stack_.resize(base + entry->num_locals, Value::unit());
    frames_.push_back(CallFrame{std::move(top), 0, base});
    return execute();
}

Value VM::call_value(const Value& callee, const std::vector<Value>& args, const SourceSpan& span) {
    stack_.clear();
    frames_.clear();
    open_upvalues_.clear();
    open_stack_ptrs_.clear();

    if (auto* cl = std::get_if<ValClosure>(&callee.node)) {
        if (args.size() != cl->proto->arity) {
            runtime_error("arity mismatch calling '" + cl->proto->name + "'", span);
        }
        stack_.push_back(callee);
        size_t base = stack_.size();
        for (const auto& a : args) stack_.push_back(a);
        if (cl->proto->num_locals > args.size()) {
            stack_.resize(base + cl->proto->num_locals, Value::unit());
        }
        frames_.push_back(CallFrame{*cl, 0, base});
        return execute();
    }

    stack_.push_back(callee);
    for (const auto& a : args) stack_.push_back(a);
    call_into_frame(callee, args.size(), false, span);
    if (frames_.empty()) {
        Value res = std::move(stack_.back());
        stack_.pop_back();
        return res;
    }
    return execute();
}

void VM::call_into_frame(const Value& callee, size_t arg_count, bool tail_call,
                         const SourceSpan& span) {
    size_t callee_pos = stack_.size() - 1 - arg_count;

    if (auto* cl = std::get_if<ValClosure>(&callee.node)) {
        if (arg_count != cl->proto->arity) {
            runtime_error("expected " + std::to_string(cl->proto->arity) + " argument(s), got " +
                              std::to_string(arg_count),
                          span);
        }
        if (tail_call && !frames_.empty()) {
            CallFrame& cur = frames_.back();
            close_upvalues(cur.base_slot);
            size_t new_callee_pos = cur.base_slot - 1;
            for (size_t i = 0; i <= arg_count; ++i) {
                stack_[new_callee_pos + i] = std::move(stack_[callee_pos + i]);
            }
            stack_.resize(new_callee_pos + 1 + std::max(arg_count, cl->proto->num_locals),
                          Value::unit());
            cur.closure = *cl;
            cur.ip = 0;
            return;
        }
        size_t base = callee_pos + 1;
        if (cl->proto->num_locals > arg_count) {
            stack_.resize(base + cl->proto->num_locals, Value::unit());
        }
        frames_.push_back(CallFrame{*cl, 0, base});
        return;
    }

    if (auto* nat = std::get_if<ValNative>(&callee.node)) {
        std::vector<Value> all_args = nat->bound_args;
        for (size_t i = 0; i < arg_count; ++i) {
            all_args.push_back(stack_[callee_pos + 1 + i]);
        }
        if (all_args.size() < nat->arity) {
            ValNative partial{nat->name, nat->arity, std::move(all_args), nat->fn};
            stack_.resize(callee_pos);
            stack_.push_back(Value{std::move(partial)});
            return;
        }
        std::vector<Value> call_args(all_args.begin(),
                                     all_args.begin() + static_cast<ptrdiff_t>(nat->arity));
        Value res = nat->fn(call_args);
        stack_.resize(callee_pos);
        stack_.push_back(std::move(res));
        if (all_args.size() > nat->arity) {
            size_t rem = all_args.size() - nat->arity;
            for (size_t i = nat->arity; i < all_args.size(); ++i) {
                stack_.push_back(std::move(all_args[i]));
            }
            Value next_callee = stack_[callee_pos];
            call_into_frame(next_callee, rem, tail_call, span);
        }
        return;
    }

    if (auto* ctor = std::get_if<ValCtor>(&callee.node)) {
        std::vector<Value> all_args = ctor->bound_args;
        for (size_t i = 0; i < arg_count; ++i) {
            all_args.push_back(stack_[callee_pos + 1 + i]);
        }
        if (all_args.size() < ctor->arity) {
            ValCtor partial{ctor->tag, ctor->name, ctor->arity, std::move(all_args)};
            stack_.resize(callee_pos);
            stack_.push_back(Value{std::move(partial)});
            return;
        }
        if (all_args.size() > ctor->arity) {
            runtime_error("too many arguments to constructor '" + ctor->name + "'", span);
        }
        stack_.resize(callee_pos);
        stack_.push_back(Value::make_adt(ctor->tag, ctor->name, std::move(all_args)));
        return;
    }

    runtime_error("attempted to call non-function value: " + value_to_string(callee), span);
}

Value VM::execute() {
    while (!frames_.empty()) {
        CallFrame& frame = frames_.back();
        const Chunk& chunk = frame.closure.proto->chunk;
        if (frame.ip >= chunk.code.size()) {
            close_upvalues(frame.base_slot);
            Value ret = stack_.back();
            stack_.resize(frame.base_slot - 1);
            frames_.pop_back();
            if (frames_.empty()) return ret;
            stack_.push_back(std::move(ret));
            continue;
        }

        const Instruction ins = chunk.code[frame.ip++];
        switch (ins.op) {
            case OpCode::Const:
                stack_.push_back(chunk.constants[static_cast<size_t>(ins.arg1)]);
                break;

            case OpCode::Pop:
                stack_.pop_back();
                break;

            case OpCode::Dup:
                stack_.push_back(stack_.back());
                break;

            case OpCode::GetLocal:
                stack_.push_back(stack_[frame.base_slot + static_cast<size_t>(ins.arg1)]);
                break;

            case OpCode::SetLocal:
                stack_[frame.base_slot + static_cast<size_t>(ins.arg1)] = std::move(stack_.back());
                stack_.pop_back();
                break;

            case OpCode::GetUpvalue: {
                const auto& uv = frame.closure.upvalues[static_cast<size_t>(ins.arg1)];
                stack_.push_back(uv->is_closed ? uv->closed : stack_[uv->stack_index]);
                break;
            }

            case OpCode::SetUpvalue: {
                auto& uv = frame.closure.upvalues[static_cast<size_t>(ins.arg1)];
                if (uv->is_closed) {
                    uv->closed = std::move(stack_.back());
                } else {
                    stack_[uv->stack_index] = std::move(stack_.back());
                }
                stack_.pop_back();
                break;
            }

            case OpCode::CloseUpvalue:
                close_upvalues(frame.base_slot + static_cast<size_t>(ins.arg1));
                break;

            case OpCode::GetGlobal: {
                const std::string& name = chunk.names[static_cast<size_t>(ins.arg1)];
                auto it = globals_.find(name);
                if (it == globals_.end()) {
                    runtime_error("undefined global '" + name + "'", ins.span);
                }
                stack_.push_back(it->second);
                break;
            }

            case OpCode::DefGlobal: {
                const std::string& name = chunk.names[static_cast<size_t>(ins.arg1)];
                globals_[name] = std::move(stack_.back());
                stack_.pop_back();
                break;
            }

            case OpCode::NegInt: {
                int64_t v = std::get<ValInt>(stack_.back().node).value;
                stack_.back() = Value::make_int(-v);
                break;
            }

            case OpCode::NegFloat: {
                double v = std::get<ValFloat>(stack_.back().node).value;
                stack_.back() = Value::make_float(-v);
                break;
            }

            case OpCode::BitNot: {
                int64_t v = std::get<ValInt>(stack_.back().node).value;
                stack_.back() = Value::make_int(~v);
                break;
            }

            case OpCode::LogicalNot: {
                bool v = std::get<ValBool>(stack_.back().node).value;
                stack_.back() = Value::make_bool(!v);
                break;
            }

            case OpCode::AddInt:
            case OpCode::SubInt:
            case OpCode::MulInt:
            case OpCode::DivInt:
            case OpCode::ModInt:
            case OpCode::AddFloat:
            case OpCode::SubFloat:
            case OpCode::MulFloat:
            case OpCode::DivFloat:
            case OpCode::BitAnd:
            case OpCode::BitOr:
            case OpCode::BitXor:
            case OpCode::Shl:
            case OpCode::Shr:
            case OpCode::ConcatStr:
            case OpCode::Cons:
            case OpCode::Eq:
            case OpCode::Neq:
            case OpCode::Lt:
            case OpCode::Le:
            case OpCode::Gt:
            case OpCode::Ge: {
                Value b = std::move(stack_.back());
                stack_.pop_back();
                Value a = std::move(stack_.back());
                stack_.back() = execute_binary_op(ins.op, a, b, ins.span);
                break;
            }

            case OpCode::MakeTuple: {
                size_t count = static_cast<size_t>(ins.arg1);
                std::vector<Value> elems(stack_.end() - static_cast<ptrdiff_t>(count), stack_.end());
                stack_.resize(stack_.size() - count);
                stack_.push_back(Value::make_tuple(std::move(elems)));
                break;
            }

            case OpCode::MakeList: {
                size_t count = static_cast<size_t>(ins.arg1);
                std::vector<Value> elems(stack_.end() - static_cast<ptrdiff_t>(count), stack_.end());
                stack_.resize(stack_.size() - count);
                stack_.push_back(Value::make_list(std::move(elems)));
                break;
            }

            case OpCode::MakeAdt: {
                uint32_t tag = static_cast<uint32_t>(ins.arg1);
                const std::string& name = chunk.names[static_cast<size_t>(ins.arg2)];
                stack_.push_back(Value::make_adt(tag, name, {}));
                break;
            }

            case OpCode::MakeStruct: {
                const std::string& type_name = chunk.names[static_cast<size_t>(ins.arg1)];
                size_t count = static_cast<size_t>(ins.arg2);
                size_t start = stack_.size() - 2 * count;
                std::unordered_map<std::string, Value> fields;
                fields.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    const std::string& k = std::get<ValString>(stack_[start + 2 * i].node).value;
                    fields[k] = stack_[start + 2 * i + 1];
                }
                stack_.resize(start);
                stack_.push_back(Value::make_struct(type_name, std::move(fields)));
                break;
            }

            case OpCode::GetField: {
                const std::string& field = chunk.names[static_cast<size_t>(ins.arg1)];
                Value obj = std::move(stack_.back());
                stack_.pop_back();
                auto* st = std::get_if<ValStruct>(&obj.node);
                if (!st) {
                    runtime_error("cannot access field '" + field + "' on non-struct value",
                                  ins.span);
                }
                auto it = st->ptr->fields.find(field);
                if (it == st->ptr->fields.end()) {
                    runtime_error("unknown field '" + field + "' on '" + st->ptr->type_name + "'",
                                  ins.span);
                }
                stack_.push_back(it->second);
                break;
            }

            case OpCode::SetField: {
                const std::string& field = chunk.names[static_cast<size_t>(ins.arg1)];
                Value val = std::move(stack_.back());
                stack_.pop_back();
                Value obj = std::move(stack_.back());
                stack_.pop_back();
                auto* st = std::get_if<ValStruct>(&obj.node);
                if (!st) {
                    runtime_error("cannot assign field '" + field + "' on non-struct value",
                                  ins.span);
                }
                st->ptr->fields[field] = std::move(val);
                stack_.push_back(Value::unit());
                break;
            }

            case OpCode::NewHeap: {
                Value v = std::move(stack_.back());
                stack_.back() = Value::make_heap_ptr(std::move(v));
                break;
            }

            case OpCode::CloseHeap: {
                Value p = std::move(stack_.back());
                stack_.pop_back();
                auto* pv = std::get_if<ValPtr>(&p.node);
                if (!pv || !pv->ptr) {
                    runtime_error("expected pointer in 'close'", ins.span);
                }
                if (!pv->ptr->is_heap) {
                    runtime_error("cannot 'close' non-heap pointer (only pointers from 'new' are heap-allocated)",
                                  ins.span);
                }
                if (!pv->ptr->alive) {
                    runtime_error("double close on heap pointer", ins.span);
                }
                pv->ptr->alive = false;
                pv->ptr->value = Value::unit();
                live_heap_allocations().fetch_sub(1, std::memory_order_relaxed);
                stack_.push_back(Value::unit());
                break;
            }

            case OpCode::DerefPtr: {
                Value p = std::move(stack_.back());
                stack_.pop_back();
                auto* pv = std::get_if<ValPtr>(&p.node);
                if (!pv) runtime_error("cannot dereference non-pointer value", ins.span);
                stack_.push_back(read_ptr_cell(pv->ptr, ins.span));
                break;
            }

            case OpCode::SetDeref: {
                Value val = std::move(stack_.back());
                stack_.pop_back();
                Value p = std::move(stack_.back());
                stack_.pop_back();
                auto* pv = std::get_if<ValPtr>(&p.node);
                if (!pv) runtime_error("cannot assign through non-pointer value", ins.span);
                write_ptr_cell(pv->ptr, std::move(val), ins.span);
                stack_.push_back(Value::unit());
                break;
            }

            case OpCode::GetPtrField: {
                const std::string& field = chunk.names[static_cast<size_t>(ins.arg1)];
                Value p = std::move(stack_.back());
                stack_.pop_back();
                auto* pv = std::get_if<ValPtr>(&p.node);
                if (!pv) runtime_error("cannot access '->" + field + "' on non-pointer value", ins.span);
                const Value& target = read_ptr_cell(pv->ptr, ins.span);
                auto* st = std::get_if<ValStruct>(&target.node);
                if (!st) {
                    runtime_error("cannot access '->" + field + "' on pointer to non-struct value",
                                  ins.span);
                }
                auto it = st->ptr->fields.find(field);
                if (it == st->ptr->fields.end()) {
                    runtime_error("unknown field '" + field + "' on '" + st->ptr->type_name + "'",
                                  ins.span);
                }
                stack_.push_back(it->second);
                break;
            }

            case OpCode::SetPtrField: {
                const std::string& field = chunk.names[static_cast<size_t>(ins.arg1)];
                Value val = std::move(stack_.back());
                stack_.pop_back();
                Value p = std::move(stack_.back());
                stack_.pop_back();
                auto* pv = std::get_if<ValPtr>(&p.node);
                if (!pv) runtime_error("cannot assign '->" + field + "' on non-pointer value", ins.span);
                const Value& target = read_ptr_cell(pv->ptr, ins.span);
                auto* st = std::get_if<ValStruct>(&target.node);
                if (!st) {
                    runtime_error("cannot assign '->" + field + "' on pointer to non-struct value",
                                  ins.span);
                }
                st->ptr->fields[field] = std::move(val);
                stack_.push_back(Value::unit());
                break;
            }

            case OpCode::AddrOfLocal: {
                if (ins.arg1 < 0) {
                    Value v = std::move(stack_.back());
                    stack_.back() = Value{ValPtr{make_rc<HeapCell>(std::move(v), false)}};
                } else {
                    size_t slot = frame.base_slot + static_cast<size_t>(ins.arg1);
                    Rc<HeapCell> existing;
                    for (const auto& c : open_stack_ptrs_) {
                        if (c->alive && c->stack_slot == slot) {
                            existing = c;
                            break;
                        }
                    }
                    if (!existing) {
                        existing = make_rc<HeapCell>(slot);
                        open_stack_ptrs_.push_back(existing);
                    }
                    stack_.push_back(Value{ValPtr{std::move(existing)}});
                }
                break;
            }

            case OpCode::AddrOfGlobal: {
                const std::string& name = chunk.names[static_cast<size_t>(ins.arg1)];
                if (!globals_.count(name)) {
                    runtime_error("undefined global '" + name + "' in address-of", ins.span);
                }
                stack_.push_back(Value::make_global_ptr(name));
                break;
            }

            case OpCode::IterItems: {
                Value col = std::move(stack_.back());
                stack_.pop_back();
                if (std::holds_alternative<ValList>(col.node)) {
                    stack_.push_back(col);
                } else if (auto* vec = std::get_if<ValVector>(&col.node)) {
                    stack_.push_back(Value::make_list(vec->ptr->elems));
                } else if (auto* set = std::get_if<ValSet>(&col.node)) {
                    stack_.push_back(Value::make_list(set->ptr->elems));
                } else if (auto* dict = std::get_if<ValDict>(&col.node)) {
                    std::vector<Value> items;
                    items.reserve(dict->ptr->entries.size());
                    for (const auto& [k, v] : dict->ptr->entries) {
                        items.push_back(Value::make_tuple({k, v}));
                    }
                    stack_.push_back(Value::make_list(std::move(items)));
                } else if (auto* str = std::get_if<ValString>(&col.node)) {
                    std::vector<Value> chars;
                    chars.reserve(str->value.size());
                    for (char c : str->value) {
                        chars.push_back(Value::make_string(std::string(1, c)));
                    }
                    stack_.push_back(Value::make_list(std::move(chars)));
                } else {
                    runtime_error("value is not iterable in 'for each'", ins.span);
                }
                break;
            }

            case OpCode::MakeClosure: {
                ValClosure templ = std::get<ValClosure>(
                    chunk.constants[static_cast<size_t>(ins.arg1)].node);
                templ.upvalues.reserve(templ.proto->upvalues.size());
                for (const auto& desc : templ.proto->upvalues) {
                    if (desc.is_local) {
                        templ.upvalues.push_back(capture_upvalue(frame.base_slot + desc.index));
                    } else {
                        templ.upvalues.push_back(frame.closure.upvalues[desc.index]);
                    }
                }
                stack_.push_back(Value{std::move(templ)});
                break;
            }

            case OpCode::MakeQuote: {
                const QuoteTemplate& qt = chunk.quotes[static_cast<size_t>(ins.arg1)];
                size_t count = static_cast<size_t>(ins.arg2);
                std::vector<Value> slots(stack_.end() - static_cast<ptrdiff_t>(count), stack_.end());
                stack_.resize(stack_.size() - count);
                stack_.push_back(instantiate_quote(qt, slots, ins.span));
                break;
            }

            case OpCode::Jump:
                frame.ip = static_cast<size_t>(ins.arg1);
                break;

            case OpCode::JumpIfFalse:
                if (!is_truthy(stack_.back())) {
                    frame.ip = static_cast<size_t>(ins.arg1);
                }
                break;

            case OpCode::JumpIfTrue:
                if (is_truthy(stack_.back())) {
                    frame.ip = static_cast<size_t>(ins.arg1);
                }
                break;

            case OpCode::Call:
            case OpCode::TailCall: {
                size_t arg_count = static_cast<size_t>(ins.arg1);
                Value callee = stack_[stack_.size() - 1 - arg_count];
                call_into_frame(callee, arg_count, ins.op == OpCode::TailCall, ins.span);
                break;
            }

            case OpCode::Return: {
                close_upvalues(frame.base_slot);
                Value ret = std::move(stack_.back());
                stack_.resize(frame.base_slot - 1);
                frames_.pop_back();
                if (frames_.empty()) {
                    return ret;
                }
                stack_.push_back(std::move(ret));
                break;
            }

            case OpCode::MatchBool: {
                Value v = std::move(stack_.back());
                stack_.pop_back();
                auto* b = std::get_if<ValBool>(&v.node);
                if (!b || b->value != (ins.arg1 != 0)) {
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchInt: {
                Value v = std::move(stack_.back());
                stack_.pop_back();
                auto* iv = std::get_if<ValInt>(&v.node);
                int64_t expected =
                    std::get<ValInt>(chunk.constants[static_cast<size_t>(ins.arg1)].node).value;
                if (!iv || iv->value != expected) {
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchFloat: {
                Value v = std::move(stack_.back());
                stack_.pop_back();
                auto* fv = std::get_if<ValFloat>(&v.node);
                double expected =
                    std::get<ValFloat>(chunk.constants[static_cast<size_t>(ins.arg1)].node).value;
                if (!fv || fv->value != expected) {
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchString: {
                Value v = std::move(stack_.back());
                stack_.pop_back();
                auto* sv = std::get_if<ValString>(&v.node);
                const std::string& expected =
                    std::get<ValString>(chunk.constants[static_cast<size_t>(ins.arg1)].node).value;
                if (!sv || sv->value != expected) {
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchUnit: {
                Value v = std::move(stack_.back());
                stack_.pop_back();
                if (!std::holds_alternative<ValUnit>(v.node)) {
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchTuple: {
                auto* tv = std::get_if<ValTuple>(&stack_.back().node);
                if (!tv || tv->ptr->elems.size() != static_cast<size_t>(ins.arg1)) {
                    stack_.pop_back();
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchListLen: {
                auto* lv = std::get_if<ValList>(&stack_.back().node);
                if (!lv || lv->ptr->elems.size() != static_cast<size_t>(ins.arg1)) {
                    stack_.pop_back();
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchCons: {
                auto* lv = std::get_if<ValList>(&stack_.back().node);
                if (!lv || lv->ptr->elems.empty()) {
                    stack_.pop_back();
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchAdt: {
                auto* av = std::get_if<ValAdt>(&stack_.back().node);
                if (!av || av->ptr->tag != static_cast<uint32_t>(ins.arg1)) {
                    stack_.pop_back();
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::TupleGet: {
                const auto& tup = std::get<ValTuple>(stack_.back().node);
                Value elem = tup.ptr->elems[static_cast<size_t>(ins.arg1)];
                stack_.push_back(std::move(elem));
                break;
            }

            case OpCode::ListGet: {
                const auto& lst = std::get<ValList>(stack_.back().node);
                Value elem = lst.ptr->elems[static_cast<size_t>(ins.arg1)];
                stack_.push_back(std::move(elem));
                break;
            }

            case OpCode::ListTail: {
                const auto& lst = std::get<ValList>(stack_.back().node);
                size_t start = static_cast<size_t>(ins.arg1);
                std::vector<Value> tail(lst.ptr->elems.begin() + static_cast<ptrdiff_t>(start),
                                        lst.ptr->elems.end());
                stack_.push_back(Value::make_list(std::move(tail)));
                break;
            }

            case OpCode::AdtField: {
                const auto& adt = std::get<ValAdt>(stack_.back().node);
                Value f = adt.ptr->fields[static_cast<size_t>(ins.arg1)];
                stack_.push_back(std::move(f));
                break;
            }

            case OpCode::MatchFail:
                runtime_error("non-exhaustive pattern match failed", ins.span);
        }
    }
    return Value::unit();
}

}  // namespace cml
