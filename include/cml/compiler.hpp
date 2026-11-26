#pragma once

#include "cml/ast.hpp"
#include "cml/bytecode.hpp"
#include "cml/typecheck.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cml {

class Compiler {
public:
    using ModuleGlobalIdFn =
        std::function<std::string(const std::string& path, const SourceSpan& span)>;

    explicit Compiler(const std::unordered_map<std::string, CtorInfo>* ctors = nullptr);

    void set_ctors(const std::unordered_map<std::string, CtorInfo>* ctors) { ctors_ = ctors; }
    void set_module_resolver(ModuleGlobalIdFn fn) { module_resolver_ = std::move(fn); }

    std::shared_ptr<FunctionProto> compile_program(const Program& prog);
    std::shared_ptr<FunctionProto> compile_program_with_modules(
        const Program& prog,
        const std::vector<std::pair<std::string, std::shared_ptr<FunctionProto>>>& module_inits);
    std::shared_ptr<FunctionProto> compile_module_program(
        const Program& prog, const std::vector<std::string>& export_names);

    std::shared_ptr<FunctionProto> compile_stmt(const StmtPtr& stmt);
    std::shared_ptr<FunctionProto> compile_expr(const ExprPtr& expr,
                                                const std::vector<Ident>& params = {});

private:
    struct LocalVar {
        Ident ident;
        uint32_t slot = 0;
        bool is_captured = false;
    };

    struct DeferFrame {
        std::vector<ExprPtr> actions;
    };

    struct FnState {
        std::shared_ptr<FunctionProto> proto;
        FnState* enclosing = nullptr;
        std::vector<LocalVar> locals;
        std::vector<UpvalueDesc> upvalues;
        std::vector<DeferFrame> defer_frames;
        uint32_t max_locals = 0;
    };

    struct LoopContext {
        size_t local_mark = 0;
        size_t defer_depth = 0;
        std::vector<size_t> break_jumps;
        std::vector<size_t> continue_jumps;
    };

    // State & scope management (src/codegen/compiler_state.cpp)
    void begin_fn(const std::string& name, size_t arity);
    std::shared_ptr<FunctionProto> end_fn();
    Chunk& chunk();
    uint32_t alloc_local(const Ident& ident);
    int resolve_local(FnState* fn, const Ident& ident) const;
    int resolve_upvalue(FnState* fn, const Ident& ident);
    uint32_t add_upvalue(FnState* fn, bool is_local, uint32_t index);
    size_t emit(OpCode op, int32_t a1 = 0, int32_t a2 = 0, const SourceSpan& span = {});
    size_t emit_jump(OpCode op, const SourceSpan& span = {});
    void patch_jump(size_t idx);
    void pop_locals_to(size_t count, const SourceSpan& span);
    void emit_defer_actions(const std::vector<ExprPtr>& actions, const SourceSpan& span);
    void emit_defers_down_to(size_t target_depth, const SourceSpan& span);

    // Pattern compilation (src/codegen/compiler_pat.cpp)
    // Expects scrutinee on top of stack; binds matched vars and pops scrutinee on success.
    // On failure, jumps to fail_jumps (after popping scrutinee).
    void compile_pattern_match(const PatternPtr& pat, std::vector<size_t>& fail_jumps);
    // Irrefutable or statement-level pattern binding (throws MatchFail on mismatch).
    void compile_pattern_bind(const PatternPtr& pat, bool is_global);
    void collect_pattern_binders(const PatternPtr& pat, std::vector<Ident>& out) const;

    // Expression & statement compilation (src/codegen/compiler_expr.cpp)
    void compile_stmt_impl(const StmtPtr& stmt, bool keep_value);
    void compile_expr_impl(const ExprPtr& expr, bool tail_pos);
    void compile_let_expr(const ExprLet& let_expr, const SourceSpan& span, bool tail_pos);
    void compile_fn_expr(const ExprFn& fn_expr, const SourceSpan& span);
    void compile_match_expr(const ExprMatch& match_expr, const SourceSpan& span, bool tail_pos);
    void compile_quote_expr(const ExprQuote& quote_expr, const SourceSpan& span);

    FnState* current_ = nullptr;
    const std::unordered_map<std::string, CtorInfo>* ctors_ = nullptr;
    ModuleGlobalIdFn module_resolver_;
    std::vector<LoopContext> loop_stack_;
};

}  // namespace cml
