#pragma once

#include "cml/bytecode.hpp"
#include "cml/typecheck.hpp"
#include "cml/value.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cml {

class VM {
public:
    struct CallFrame {
        ValClosure closure;
        size_t ip = 0;
        size_t base_slot = 0;
    };

    using DebugHook = std::function<void(const VM&, const Instruction&)>;

    VM();

    void register_builtins();
    void register_ctors(const std::unordered_map<std::string, CtorInfo>& ctors);
    void set_global(const std::string& name, Value val);
    const Value* get_global(const std::string& name) const;
    const std::unordered_map<std::string, Value>& globals() const { return globals_; }
    const std::vector<CallFrame>& frames() const { return frames_; }
    const std::vector<Value>& stack() const { return stack_; }
    void set_debug_hook(DebugHook hook) { debug_hook_ = std::move(hook); }

    Value run(const std::shared_ptr<FunctionProto>& entry);
    Value call_value(const Value& callee, const std::vector<Value>& args, const SourceSpan& span);

private:

    Value execute();
    Rc<UpvalueObj> capture_upvalue(size_t stack_slot);
    void close_upvalues(size_t from_slot);
    void invalidate_stack_ptrs(size_t from_slot);
    const Value& read_ptr_cell(const Rc<HeapCell>& cell, const SourceSpan& span) const;
    void write_ptr_cell(const Rc<HeapCell>& cell, Value val, const SourceSpan& span);
    void call_into_frame(const Value& callee, size_t arg_count, bool tail_call,
                         const SourceSpan& span);
    Value execute_binary_op(OpCode op, const Value& a, const Value& b, const SourceSpan& span);
    Value instantiate_quote(const QuoteTemplate& qt, const std::vector<Value>& slots,
                            const SourceSpan& span);
    [[noreturn]] void runtime_error(const std::string& msg, const SourceSpan& span) const;

    std::vector<Value> stack_;
    std::vector<CallFrame> frames_;
    std::vector<Rc<UpvalueObj>> open_upvalues_;
    std::vector<Rc<HeapCell>> open_stack_ptrs_;
    std::unordered_map<std::string, Value> globals_;
    DebugHook debug_hook_;
};

}  // namespace cml
