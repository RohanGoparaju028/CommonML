#pragma once

#include "cml/bytecode.hpp"
#include "cml/typecheck.hpp"
#include "cml/value.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cml {

struct UpvalueObj {
    size_t stack_index = 0;
    Value closed;
    bool is_closed = false;
};

class VM {
public:
    VM();

    void register_builtins();
    void register_ctors(const std::unordered_map<std::string, CtorInfo>& ctors);
    void set_global(const std::string& name, Value val);
    const Value* get_global(const std::string& name) const;
    const std::unordered_map<std::string, Value>& globals() const { return globals_; }

    Value run(const std::shared_ptr<FunctionProto>& entry);
    Value call_value(const Value& callee, const std::vector<Value>& args, const SourceSpan& span);

private:
    struct CallFrame {
        ValClosure closure;
        size_t ip = 0;
        size_t base_slot = 0;
    };

    Value execute();
    std::shared_ptr<UpvalueObj> capture_upvalue(size_t stack_slot);
    void close_upvalues(size_t from_slot);
    void call_into_frame(const Value& callee, size_t arg_count, bool tail_call,
                         const SourceSpan& span);
    Value execute_binary_op(OpCode op, const Value& a, const Value& b, const SourceSpan& span);
    Value instantiate_quote(const QuoteTemplate& qt, const std::vector<Value>& slots,
                            const SourceSpan& span);
    [[noreturn]] void runtime_error(const std::string& msg, const SourceSpan& span) const;

    std::vector<Value> stack_;
    std::vector<CallFrame> frames_;
    std::vector<std::shared_ptr<UpvalueObj>> open_upvalues_;
    std::unordered_map<std::string, Value> globals_;
};

}  // namespace cml
