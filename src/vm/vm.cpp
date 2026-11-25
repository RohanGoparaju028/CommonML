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

std::shared_ptr<UpvalueObj> VM::capture_upvalue(size_t stack_slot) {
    for (const auto& uv : open_upvalues_) {
        if (!uv->is_closed && uv->stack_index == stack_slot) {
            return uv;
        }
    }
    auto uv = std::make_shared<UpvalueObj>();
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
}

Value VM::run(const std::shared_ptr<FunctionProto>& entry) {
    stack_.clear();
    frames_.clear();
    open_upvalues_.clear();

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

            case OpCode::AddInt:
            case OpCode::SubInt:
            case OpCode::MulInt:
            case OpCode::DivInt:
            case OpCode::ModInt:
            case OpCode::AddFloat:
            case OpCode::SubFloat:
            case OpCode::MulFloat:
            case OpCode::DivFloat:
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
                if (!tv || tv->elems.size() != static_cast<size_t>(ins.arg1)) {
                    stack_.pop_back();
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchListLen: {
                auto* lv = std::get_if<ValList>(&stack_.back().node);
                if (!lv || lv->elems.size() != static_cast<size_t>(ins.arg1)) {
                    stack_.pop_back();
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchCons: {
                auto* lv = std::get_if<ValList>(&stack_.back().node);
                if (!lv || lv->elems.empty()) {
                    stack_.pop_back();
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::MatchAdt: {
                auto* av = std::get_if<ValAdt>(&stack_.back().node);
                if (!av || av->tag != static_cast<uint32_t>(ins.arg1)) {
                    stack_.pop_back();
                    frame.ip = static_cast<size_t>(ins.arg2);
                }
                break;
            }

            case OpCode::TupleGet: {
                const auto& tup = std::get<ValTuple>(stack_.back().node);
                Value elem = tup.elems[static_cast<size_t>(ins.arg1)];
                stack_.push_back(std::move(elem));
                break;
            }

            case OpCode::ListGet: {
                const auto& lst = std::get<ValList>(stack_.back().node);
                Value elem = lst.elems[static_cast<size_t>(ins.arg1)];
                stack_.push_back(std::move(elem));
                break;
            }

            case OpCode::ListTail: {
                const auto& lst = std::get<ValList>(stack_.back().node);
                size_t start = static_cast<size_t>(ins.arg1);
                std::vector<Value> tail(lst.elems.begin() + static_cast<ptrdiff_t>(start),
                                        lst.elems.end());
                stack_.push_back(Value::make_list(std::move(tail)));
                break;
            }

            case OpCode::AdtField: {
                const auto& adt = std::get<ValAdt>(stack_.back().node);
                Value f = adt.fields[static_cast<size_t>(ins.arg1)];
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
