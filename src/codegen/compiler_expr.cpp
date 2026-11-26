#include "cml/compiler.hpp"

#include <functional>

namespace cml {

std::shared_ptr<FunctionProto> Compiler::compile_program(const Program& prog) {
    return compile_program_with_modules(prog, {});
}

std::shared_ptr<FunctionProto> Compiler::compile_program_with_modules(
    const Program& prog,
    const std::vector<std::pair<std::string, std::shared_ptr<FunctionProto>>>& module_inits) {
    begin_fn("<script>", 0);
    current_->defer_frames.push_back(DeferFrame{});
    for (const auto& [mod_global, mod_proto] : module_inits) {
        uint32_t cidx = chunk().add_constant(Value{ValClosure{mod_proto, {}}});
        uint32_t nidx = chunk().add_name(mod_global);
        emit(OpCode::MakeClosure, static_cast<int32_t>(cidx));
        emit(OpCode::Call, 0);
        emit(OpCode::DefGlobal, static_cast<int32_t>(nidx));
    }
    if (prog.stmts.empty()) {
        uint32_t idx = chunk().add_constant(Value::unit());
        emit(OpCode::Const, static_cast<int32_t>(idx));
        current_->defer_frames.pop_back();
        emit(OpCode::Return);
        return end_fn();
    }
    for (size_t i = 0; i < prog.stmts.size(); ++i) {
        bool is_last = (i + 1 == prog.stmts.size());
        compile_stmt_impl(prog.stmts[i], is_last);
    }
    if (!current_->defer_frames.back().actions.empty()) {
        uint32_t res_slot = alloc_local(Ident{"$script_res", {}, {}});
        emit(OpCode::SetLocal, static_cast<int32_t>(res_slot));
        emit_defer_actions(current_->defer_frames.back().actions, {});
        emit(OpCode::GetLocal, static_cast<int32_t>(res_slot));
    }
    current_->defer_frames.pop_back();
    emit(OpCode::Return);
    return end_fn();
}

std::shared_ptr<FunctionProto> Compiler::compile_module_program(
    const Program& prog, const std::vector<std::string>& export_names) {
    begin_fn("<module>", 0);
    for (const auto& stmt : prog.stmts) {
        compile_stmt_impl(stmt, false);
    }
    uint32_t mod_type_idx = chunk().add_name("$Module");
    for (const auto& name : export_names) {
        uint32_t key_idx = chunk().add_constant(Value::make_string(name));
        emit(OpCode::Const, static_cast<int32_t>(key_idx));
        if (ctors_) {
            auto cit = ctors_->find(name);
            if (cit != ctors_->end() && cit->second.arity == 0) {
                uint32_t cname_idx = chunk().add_name(name);
                emit(OpCode::MakeAdt, static_cast<int32_t>(cit->second.tag),
                     static_cast<int32_t>(cname_idx));
                continue;
            }
        }
        uint32_t name_idx = chunk().add_name(name);
        emit(OpCode::GetGlobal, static_cast<int32_t>(name_idx));
    }
    emit(OpCode::MakeStruct, static_cast<int32_t>(mod_type_idx),
         static_cast<int32_t>(export_names.size()));
    emit(OpCode::Return);
    return end_fn();
}

std::shared_ptr<FunctionProto> Compiler::compile_stmt(const StmtPtr& stmt) {
    begin_fn("<repl>", 0);
    if (!stmt) {
        uint32_t idx = chunk().add_constant(Value::unit());
        emit(OpCode::Const, static_cast<int32_t>(idx));
    } else {
        compile_stmt_impl(stmt, true);
    }
    emit(OpCode::Return);
    return end_fn();
}

std::shared_ptr<FunctionProto> Compiler::compile_expr(const ExprPtr& expr,
                                                      const std::vector<Ident>& params) {
    begin_fn("<expr>", params.size());
    for (const auto& p : params) {
        alloc_local(p);
    }
    compile_expr_impl(expr, true);
    emit(OpCode::Return);
    return end_fn();
}

void Compiler::compile_stmt_impl(const StmtPtr& stmt, bool keep_value) {
    if (!stmt) {
        if (keep_value) {
            uint32_t idx = chunk().add_constant(Value::unit());
            emit(OpCode::Const, static_cast<int32_t>(idx));
        }
        return;
    }

    std::visit(
        [&](const auto& n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, StmtTypeDecl> || std::is_same_v<T, StmtStructDecl> ||
                          std::is_same_v<T, StmtMacro> || std::is_same_v<T, StmtImport>) {
                if (keep_value) {
                    uint32_t idx = chunk().add_constant(Value::unit());
                    emit(OpCode::Const, static_cast<int32_t>(idx), 0, stmt->span);
                }
            } else if constexpr (std::is_same_v<T, StmtVar>) {
                compile_expr_impl(n.value, false);
                if (keep_value) {
                    emit(OpCode::Dup, 0, 0, stmt->span);
                }
                uint32_t name_idx = chunk().add_name(n.name.name);
                emit(OpCode::DefGlobal, static_cast<int32_t>(name_idx), 0, stmt->span);
            } else if constexpr (std::is_same_v<T, StmtLet>) {
                if (n.is_rec) {
                    const auto& pv = std::get<PatVar>(n.pat->node);
                    uint32_t unit_idx = chunk().add_constant(Value::unit());
                    uint32_t name_idx = chunk().add_name(pv.ident.name);
                    emit(OpCode::Const, static_cast<int32_t>(unit_idx), 0, stmt->span);
                    emit(OpCode::DefGlobal, static_cast<int32_t>(name_idx), 0, stmt->span);
                }
                compile_expr_impl(n.value, false);
                if (keep_value) {
                    emit(OpCode::Dup, 0, 0, stmt->span);
                }
                compile_pattern_bind(n.pat, true);
            } else {
                compile_expr_impl(n.expr, false);
                if (!keep_value) {
                    emit(OpCode::Pop, 0, 0, stmt->span);
                }
            }
        },
        stmt->node);
}

void Compiler::compile_expr_impl(const ExprPtr& expr, bool tail_pos) {
    if (!expr) {
        uint32_t idx = chunk().add_constant(Value::unit());
        emit(OpCode::Const, static_cast<int32_t>(idx));
        return;
    }

    std::visit(
        [&](const auto& n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ExprInt>) {
                uint32_t idx = chunk().add_constant(Value::make_int(n.value));
                emit(OpCode::Const, static_cast<int32_t>(idx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprFloat>) {
                uint32_t idx = chunk().add_constant(Value::make_float(n.value));
                emit(OpCode::Const, static_cast<int32_t>(idx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprBool>) {
                uint32_t idx = chunk().add_constant(Value::make_bool(n.value));
                emit(OpCode::Const, static_cast<int32_t>(idx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprString>) {
                uint32_t idx = chunk().add_constant(Value::make_string(n.value));
                emit(OpCode::Const, static_cast<int32_t>(idx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprUnit>) {
                uint32_t idx = chunk().add_constant(Value::unit());
                emit(OpCode::Const, static_cast<int32_t>(idx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprVar>) {
                int local = resolve_local(current_, n.ident);
                if (local != -1) {
                    emit(OpCode::GetLocal, local, 0, expr->span);
                    return;
                }
                int upval = resolve_upvalue(current_, n.ident);
                if (upval != -1) {
                    emit(OpCode::GetUpvalue, upval, 0, expr->span);
                    return;
                }
                uint32_t name_idx = chunk().add_name(n.ident.name);
                emit(OpCode::GetGlobal, static_cast<int32_t>(name_idx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprCtor>) {
                if (!ctors_) {
                    throw CompileError(Diagnostic{"codegen", "missing constructor table", expr->span});
                }
                auto it = ctors_->find(n.ident.name);
                if (it == ctors_->end()) {
                    throw CompileError(
                        Diagnostic{"codegen", "unknown constructor '" + n.ident.name + "'", expr->span});
                }
                if (it->second.arity == 0) {
                    emit(OpCode::MakeAdt, static_cast<int32_t>(it->second.tag),
                         static_cast<int32_t>(chunk().add_name(n.ident.name)), expr->span);
                } else {
                    uint32_t name_idx = chunk().add_name(n.ident.name);
                    emit(OpCode::GetGlobal, static_cast<int32_t>(name_idx), 0, expr->span);
                }
            } else if constexpr (std::is_same_v<T, ExprTuple>) {
                for (const auto& e : n.elems) {
                    compile_expr_impl(e, false);
                }
                emit(OpCode::MakeTuple, static_cast<int32_t>(n.elems.size()), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprList>) {
                for (const auto& e : n.elems) {
                    compile_expr_impl(e, false);
                }
                emit(OpCode::MakeList, static_cast<int32_t>(n.elems.size()), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprUnary>) {
                if (n.op == UnaryOp::AddrOf) {
                    if (auto* v = std::get_if<ExprVar>(&n.operand->node)) {
                        int local = resolve_local(current_, v->ident);
                        if (local != -1) {
                            current_->locals[static_cast<size_t>(local)].is_captured = true;
                            emit(OpCode::AddrOfLocal, local, 0, expr->span);
                            return;
                        }
                        int upval = resolve_upvalue(current_, v->ident);
                        if (upval != -1) {
                            emit(OpCode::GetUpvalue, upval, 0, expr->span);
                            emit(OpCode::AddrOfLocal, -1, 0, expr->span);
                            return;
                        }
                        uint32_t nidx = chunk().add_name(v->ident.name);
                        emit(OpCode::AddrOfGlobal, static_cast<int32_t>(nidx), 0, expr->span);
                        return;
                    }
                    compile_expr_impl(n.operand, false);
                    emit(OpCode::AddrOfLocal, -1, 0, expr->span);
                    return;
                }
                compile_expr_impl(n.operand, false);
                OpCode uop = OpCode::NegInt;
                switch (n.op) {
                    case UnaryOp::NegInt: uop = OpCode::NegInt; break;
                    case UnaryOp::NegFloat: uop = OpCode::NegFloat; break;
                    case UnaryOp::BitNot: uop = OpCode::BitNot; break;
                    case UnaryOp::LogicalNot: uop = OpCode::LogicalNot; break;
                    case UnaryOp::Deref: uop = OpCode::DerefPtr; break;
                    case UnaryOp::AddrOf: break;
                }
                emit(uop, 0, 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprBinary>) {
                if (n.op == BinaryOp::And) {
                    compile_expr_impl(n.left, false);
                    size_t false_jmp = emit_jump(OpCode::JumpIfFalse, expr->span);
                    emit(OpCode::Pop, 0, 0, expr->span);
                    compile_expr_impl(n.right, tail_pos);
                    size_t end_jmp = emit_jump(OpCode::Jump, expr->span);
                    patch_jump(false_jmp);
                    patch_jump(end_jmp);
                    return;
                }
                if (n.op == BinaryOp::Or) {
                    compile_expr_impl(n.left, false);
                    size_t true_jmp = emit_jump(OpCode::JumpIfTrue, expr->span);
                    emit(OpCode::Pop, 0, 0, expr->span);
                    compile_expr_impl(n.right, tail_pos);
                    size_t end_jmp = emit_jump(OpCode::Jump, expr->span);
                    patch_jump(true_jmp);
                    patch_jump(end_jmp);
                    return;
                }
                if (n.op == BinaryOp::Seq) {
                    compile_expr_impl(n.left, false);
                    emit(OpCode::Pop, 0, 0, expr->span);
                    compile_expr_impl(n.right, tail_pos);
                    return;
                }
                if (n.op == BinaryOp::Pipe) {
                    compile_expr_impl(n.right, false);
                    compile_expr_impl(n.left, false);
                    emit(tail_pos ? OpCode::TailCall : OpCode::Call, 1, 0, expr->span);
                    return;
                }
                compile_expr_impl(n.left, false);
                compile_expr_impl(n.right, false);
                OpCode op = OpCode::AddInt;
                switch (n.op) {
                    case BinaryOp::AddInt: op = OpCode::AddInt; break;
                    case BinaryOp::SubInt: op = OpCode::SubInt; break;
                    case BinaryOp::MulInt: op = OpCode::MulInt; break;
                    case BinaryOp::DivInt: op = OpCode::DivInt; break;
                    case BinaryOp::ModInt: op = OpCode::ModInt; break;
                    case BinaryOp::AddFloat: op = OpCode::AddFloat; break;
                    case BinaryOp::SubFloat: op = OpCode::SubFloat; break;
                    case BinaryOp::MulFloat: op = OpCode::MulFloat; break;
                    case BinaryOp::DivFloat: op = OpCode::DivFloat; break;
                    case BinaryOp::BitAnd: op = OpCode::BitAnd; break;
                    case BinaryOp::BitOr: op = OpCode::BitOr; break;
                    case BinaryOp::BitXor: op = OpCode::BitXor; break;
                    case BinaryOp::Shl: op = OpCode::Shl; break;
                    case BinaryOp::Shr: op = OpCode::Shr; break;
                    case BinaryOp::ConcatStr: op = OpCode::ConcatStr; break;
                    case BinaryOp::Cons: op = OpCode::Cons; break;
                    case BinaryOp::Eq: op = OpCode::Eq; break;
                    case BinaryOp::Neq: op = OpCode::Neq; break;
                    case BinaryOp::Lt: op = OpCode::Lt; break;
                    case BinaryOp::Le: op = OpCode::Le; break;
                    case BinaryOp::Gt: op = OpCode::Gt; break;
                    case BinaryOp::Ge: op = OpCode::Ge; break;
                    default: break;
                }
                emit(op, 0, 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprIf>) {
                compile_expr_impl(n.cond, false);
                size_t else_jmp = emit_jump(OpCode::JumpIfFalse, expr->span);
                emit(OpCode::Pop, 0, 0, expr->span);
                compile_expr_impl(n.then_branch, tail_pos);
                size_t end_jmp = emit_jump(OpCode::Jump, expr->span);
                patch_jump(else_jmp);
                emit(OpCode::Pop, 0, 0, expr->span);
                compile_expr_impl(n.else_branch, tail_pos);
                patch_jump(end_jmp);
            } else if constexpr (std::is_same_v<T, ExprLet>) {
                compile_let_expr(n, expr->span, tail_pos);
            } else if constexpr (std::is_same_v<T, ExprFn>) {
                compile_fn_expr(n, expr->span);
            } else if constexpr (std::is_same_v<T, ExprApp>) {
                compile_expr_impl(n.fn, false);
                compile_expr_impl(n.arg, false);
                emit(tail_pos ? OpCode::TailCall : OpCode::Call, 1, 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprMacroCall>) {
                throw CompileError(
                    Diagnostic{"codegen", "unexpanded macro call '" + n.name.name + "!'", expr->span});
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                compile_match_expr(n, expr->span, tail_pos);
            } else if constexpr (std::is_same_v<T, ExprAnnot>) {
                compile_expr_impl(n.expr, tail_pos);
            } else if constexpr (std::is_same_v<T, ExprQuote>) {
                compile_quote_expr(n, expr->span);
            } else if constexpr (std::is_same_v<T, ExprImport>) {
                if (!module_resolver_) {
                    throw CompileError(Diagnostic{"codegen", "missing module resolver", expr->span});
                }
                std::string mod_global = module_resolver_(n.path, expr->span);
                uint32_t nidx = chunk().add_name(mod_global);
                emit(OpCode::GetGlobal, static_cast<int32_t>(nidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprStructLit>) {
                uint32_t tname_idx = chunk().add_name(n.type_name.name);
                for (const auto& [fn, fv] : n.fields) {
                    uint32_t kidx = chunk().add_constant(Value::make_string(fn.name));
                    emit(OpCode::Const, static_cast<int32_t>(kidx), 0, fn.span);
                    compile_expr_impl(fv, false);
                }
                emit(OpCode::MakeStruct, static_cast<int32_t>(tname_idx),
                     static_cast<int32_t>(n.fields.size()), expr->span);
            } else if constexpr (std::is_same_v<T, ExprFieldGet>) {
                compile_expr_impl(n.obj, false);
                uint32_t fidx = chunk().add_name(n.field.name);
                emit(OpCode::GetField, static_cast<int32_t>(fidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprFieldSet>) {
                compile_expr_impl(n.obj, false);
                compile_expr_impl(n.value, false);
                uint32_t fidx = chunk().add_name(n.field.name);
                emit(OpCode::SetField, static_cast<int32_t>(fidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprNew>) {
                compile_expr_impl(n.value, false);
                emit(OpCode::NewHeap, 0, 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprClose>) {
                compile_expr_impl(n.ptr, false);
                emit(OpCode::CloseHeap, 0, 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprDefer>) {
                if (current_->defer_frames.empty()) {
                    throw CompileError(
                        Diagnostic{"codegen", "'defer' used outside block or function scope", expr->span});
                }
                current_->defer_frames.back().actions.push_back(n.action);
                uint32_t uidx = chunk().add_constant(Value::unit());
                emit(OpCode::Const, static_cast<int32_t>(uidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprDerefSet>) {
                compile_expr_impl(n.ptr, false);
                compile_expr_impl(n.value, false);
                emit(OpCode::SetDeref, 0, 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprPtrFieldGet>) {
                compile_expr_impl(n.ptr, false);
                uint32_t fidx = chunk().add_name(n.field.name);
                emit(OpCode::GetPtrField, static_cast<int32_t>(fidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprPtrFieldSet>) {
                compile_expr_impl(n.ptr, false);
                compile_expr_impl(n.value, false);
                uint32_t fidx = chunk().add_name(n.field.name);
                emit(OpCode::SetPtrField, static_cast<int32_t>(fidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprAssign>) {
                compile_expr_impl(n.value, false);
                int local = resolve_local(current_, n.target);
                if (local != -1) {
                    emit(OpCode::SetLocal, local, 0, expr->span);
                } else {
                    int upval = resolve_upvalue(current_, n.target);
                    if (upval != -1) {
                        emit(OpCode::SetUpvalue, upval, 0, expr->span);
                    } else {
                        uint32_t nidx = chunk().add_name(n.target.name);
                        emit(OpCode::DefGlobal, static_cast<int32_t>(nidx), 0, expr->span);
                    }
                }
                uint32_t uidx = chunk().add_constant(Value::unit());
                emit(OpCode::Const, static_cast<int32_t>(uidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprBlock>) {
                size_t mark = current_->locals.size();
                current_->defer_frames.push_back(DeferFrame{});
                if (n.stmts.empty()) {
                    uint32_t uidx = chunk().add_constant(Value::unit());
                    emit(OpCode::Const, static_cast<int32_t>(uidx), 0, expr->span);
                } else {
                    for (size_t i = 0; i < n.stmts.size(); ++i) {
                        bool is_last = (i + 1 == n.stmts.size());
                        const StmtPtr& s = n.stmts[i];
                        if (auto* se = std::get_if<StmtExpr>(&s->node)) {
                            bool allow_tail = is_last && tail_pos &&
                                              current_->defer_frames.back().actions.empty();
                            compile_expr_impl(se->expr, allow_tail);
                            if (!is_last) emit(OpCode::Pop, 0, 0, s->span);
                        } else if (auto* sv = std::get_if<StmtVar>(&s->node)) {
                            compile_expr_impl(sv->value, false);
                            uint32_t slot = alloc_local(sv->name);
                            emit(OpCode::SetLocal, static_cast<int32_t>(slot), 0, s->span);
                            if (is_last) {
                                uint32_t uidx = chunk().add_constant(Value::unit());
                                emit(OpCode::Const, static_cast<int32_t>(uidx), 0, s->span);
                            }
                        } else if (auto* sl = std::get_if<StmtLet>(&s->node)) {
                            if (sl->is_rec) {
                                const auto& pv = std::get<PatVar>(sl->pat->node);
                                uint32_t slot = alloc_local(pv.ident);
                                uint32_t uidx = chunk().add_constant(Value::unit());
                                emit(OpCode::Const, static_cast<int32_t>(uidx), 0, s->span);
                                emit(OpCode::SetLocal, static_cast<int32_t>(slot), 0, s->span);
                                compile_expr_impl(sl->value, false);
                                emit(OpCode::SetLocal, static_cast<int32_t>(slot), 0, s->span);
                            } else {
                                compile_expr_impl(sl->value, false);
                                compile_pattern_bind(sl->pat, false);
                            }
                            if (is_last) {
                                uint32_t uidx = chunk().add_constant(Value::unit());
                                emit(OpCode::Const, static_cast<int32_t>(uidx), 0, s->span);
                            }
                        } else {
                            compile_stmt_impl(s, is_last);
                        }
                    }
                }
                if (!current_->defer_frames.back().actions.empty()) {
                    uint32_t res_slot = alloc_local(Ident{"$defer_res", {}, expr->span});
                    emit(OpCode::SetLocal, static_cast<int32_t>(res_slot), 0, expr->span);
                    emit_defer_actions(current_->defer_frames.back().actions, expr->span);
                    emit(OpCode::GetLocal, static_cast<int32_t>(res_slot), 0, expr->span);
                }
                current_->defer_frames.pop_back();
                pop_locals_to(mark, expr->span);
            } else if constexpr (std::is_same_v<T, ExprWhile>) {
                size_t loop_start = chunk().code.size();
                compile_expr_impl(n.cond, false);
                size_t exit_jmp = emit_jump(OpCode::JumpIfFalse, expr->span);
                emit(OpCode::Pop, 0, 0, expr->span);
                loop_stack_.push_back(LoopContext{
                    current_->locals.size(), current_->defer_frames.size(), {}, {}});
                compile_expr_impl(n.body, false);
                emit(OpCode::Pop, 0, 0, expr->span);
                LoopContext ctx = std::move(loop_stack_.back());
                loop_stack_.pop_back();
                for (size_t cj : ctx.continue_jumps) {
                    chunk().patch_jump(cj, static_cast<int32_t>(loop_start));
                }
                emit(OpCode::Jump, static_cast<int32_t>(loop_start), 0, expr->span);
                patch_jump(exit_jmp);
                emit(OpCode::Pop, 0, 0, expr->span);
                for (size_t bj : ctx.break_jumps) patch_jump(bj);
                uint32_t uidx = chunk().add_constant(Value::unit());
                emit(OpCode::Const, static_cast<int32_t>(uidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprFor>) {
                size_t for_mark = current_->locals.size();
                if (n.init) {
                    const StmtPtr& init_s = *n.init;
                    if (auto* sv = std::get_if<StmtVar>(&init_s->node)) {
                        compile_expr_impl(sv->value, false);
                        uint32_t slot = alloc_local(sv->name);
                        emit(OpCode::SetLocal, static_cast<int32_t>(slot), 0, init_s->span);
                    } else if (auto* se = std::get_if<StmtExpr>(&init_s->node)) {
                        compile_expr_impl(se->expr, false);
                        emit(OpCode::Pop, 0, 0, init_s->span);
                    }
                }
                size_t loop_start = chunk().code.size();
                compile_expr_impl(n.cond, false);
                size_t exit_jmp = emit_jump(OpCode::JumpIfFalse, expr->span);
                emit(OpCode::Pop, 0, 0, expr->span);
                loop_stack_.push_back(LoopContext{
                    current_->locals.size(), current_->defer_frames.size(), {}, {}});
                compile_expr_impl(n.body, false);
                emit(OpCode::Pop, 0, 0, expr->span);
                LoopContext ctx = std::move(loop_stack_.back());
                loop_stack_.pop_back();
                size_t step_target = chunk().code.size();
                for (size_t cj : ctx.continue_jumps) {
                    chunk().patch_jump(cj, static_cast<int32_t>(step_target));
                }
                if (n.step) {
                    compile_expr_impl(*n.step, false);
                    emit(OpCode::Pop, 0, 0, (*n.step)->span);
                }
                emit(OpCode::Jump, static_cast<int32_t>(loop_start), 0, expr->span);
                patch_jump(exit_jmp);
                emit(OpCode::Pop, 0, 0, expr->span);
                for (size_t bj : ctx.break_jumps) patch_jump(bj);
                pop_locals_to(for_mark, expr->span);
                uint32_t uidx = chunk().add_constant(Value::unit());
                emit(OpCode::Const, static_cast<int32_t>(uidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprForEach>) {
                size_t fe_mark = current_->locals.size();
                compile_expr_impl(n.iterable, false);
                emit(OpCode::IterItems, 0, 0, expr->span);
                uint32_t list_slot = alloc_local(Ident{"$iter_list", {}, expr->span});
                emit(OpCode::SetLocal, static_cast<int32_t>(list_slot), 0, expr->span);
                uint32_t idx_slot = alloc_local(Ident{"$iter_idx", {}, expr->span});
                uint32_t zero_idx = chunk().add_constant(Value::make_int(0));
                emit(OpCode::Const, static_cast<int32_t>(zero_idx), 0, expr->span);
                emit(OpCode::SetLocal, static_cast<int32_t>(idx_slot), 0, expr->span);

                size_t loop_start = chunk().code.size();
                emit(OpCode::GetLocal, static_cast<int32_t>(idx_slot), 0, expr->span);
                uint32_t len_name = chunk().add_name("list_len");
                emit(OpCode::GetGlobal, static_cast<int32_t>(len_name), 0, expr->span);
                emit(OpCode::GetLocal, static_cast<int32_t>(list_slot), 0, expr->span);
                emit(OpCode::Call, 1, 0, expr->span);
                emit(OpCode::Lt, 0, 0, expr->span);
                size_t exit_jmp = emit_jump(OpCode::JumpIfFalse, expr->span);
                emit(OpCode::Pop, 0, 0, expr->span);

                size_t iter_bind_mark = current_->locals.size();
                uint32_t nth_name = chunk().add_name("list_nth");
                emit(OpCode::GetGlobal, static_cast<int32_t>(nth_name), 0, expr->span);
                emit(OpCode::GetLocal, static_cast<int32_t>(list_slot), 0, expr->span);
                emit(OpCode::GetLocal, static_cast<int32_t>(idx_slot), 0, expr->span);
                emit(OpCode::Call, 2, 0, expr->span);
                compile_pattern_bind(n.pat, false);

                loop_stack_.push_back(LoopContext{
                    current_->locals.size(), current_->defer_frames.size(), {}, {}});
                compile_expr_impl(n.body, false);
                emit(OpCode::Pop, 0, 0, expr->span);
                LoopContext ctx = std::move(loop_stack_.back());
                loop_stack_.pop_back();

                size_t step_target = chunk().code.size();
                for (size_t cj : ctx.continue_jumps) {
                    chunk().patch_jump(cj, static_cast<int32_t>(step_target));
                }
                pop_locals_to(iter_bind_mark, expr->span);
                emit(OpCode::GetLocal, static_cast<int32_t>(idx_slot), 0, expr->span);
                uint32_t one_idx = chunk().add_constant(Value::make_int(1));
                emit(OpCode::Const, static_cast<int32_t>(one_idx), 0, expr->span);
                emit(OpCode::AddInt, 0, 0, expr->span);
                emit(OpCode::SetLocal, static_cast<int32_t>(idx_slot), 0, expr->span);
                emit(OpCode::Jump, static_cast<int32_t>(loop_start), 0, expr->span);

                patch_jump(exit_jmp);
                emit(OpCode::Pop, 0, 0, expr->span);
                for (size_t bj : ctx.break_jumps) patch_jump(bj);
                pop_locals_to(fe_mark, expr->span);
                uint32_t uidx = chunk().add_constant(Value::unit());
                emit(OpCode::Const, static_cast<int32_t>(uidx), 0, expr->span);
            } else if constexpr (std::is_same_v<T, ExprBreak>) {
                if (loop_stack_.empty()) {
                    throw CompileError(Diagnostic{"codegen", "'break' outside of loop", expr->span});
                }
                emit_defers_down_to(loop_stack_.back().defer_depth, expr->span);
                for (size_t i = current_->locals.size(); i > loop_stack_.back().local_mark; --i) {
                    const auto& loc = current_->locals[i - 1];
                    if (loc.is_captured) {
                        emit(OpCode::CloseUpvalue, static_cast<int32_t>(loc.slot), 0, expr->span);
                    }
                }
                loop_stack_.back().break_jumps.push_back(emit_jump(OpCode::Jump, expr->span));
            } else if constexpr (std::is_same_v<T, ExprContinue>) {
                if (loop_stack_.empty()) {
                    throw CompileError(
                        Diagnostic{"codegen", "'continue' outside of loop", expr->span});
                }
                emit_defers_down_to(loop_stack_.back().defer_depth, expr->span);
                for (size_t i = current_->locals.size(); i > loop_stack_.back().local_mark; --i) {
                    const auto& loc = current_->locals[i - 1];
                    if (loc.is_captured) {
                        emit(OpCode::CloseUpvalue, static_cast<int32_t>(loc.slot), 0, expr->span);
                    }
                }
                loop_stack_.back().continue_jumps.push_back(emit_jump(OpCode::Jump, expr->span));
            } else {
                throw CompileError(Diagnostic{"codegen", "unquote outside quote template", expr->span});
            }
        },
        expr->node);
}

void Compiler::compile_let_expr(const ExprLet& let_expr, const SourceSpan& span, bool tail_pos) {
    size_t mark = current_->locals.size();
    if (let_expr.is_rec) {
        const auto& pv = std::get<PatVar>(let_expr.pat->node);
        uint32_t slot = alloc_local(pv.ident);
        uint32_t unit_idx = chunk().add_constant(Value::unit());
        emit(OpCode::Const, static_cast<int32_t>(unit_idx), 0, span);
        emit(OpCode::SetLocal, static_cast<int32_t>(slot), 0, span);

        compile_expr_impl(let_expr.value, false);
        emit(OpCode::SetLocal, static_cast<int32_t>(slot), 0, span);
    } else {
        compile_expr_impl(let_expr.value, false);
        compile_pattern_bind(let_expr.pat, false);
    }
    compile_expr_impl(let_expr.body, tail_pos);
    pop_locals_to(mark, span);
}

void Compiler::compile_fn_expr(const ExprFn& fn_expr, const SourceSpan& span) {
    begin_fn("lambda", 1);
    Ident param_ident{"$arg", {}, span};
    uint32_t param_slot = alloc_local(param_ident);
    if (auto* pv = std::get_if<PatVar>(&fn_expr.param->node)) {
        current_->locals[param_slot].ident = pv->ident;
    } else if (!std::holds_alternative<PatWildcard>(fn_expr.param->node)) {
        emit(OpCode::GetLocal, static_cast<int32_t>(param_slot), 0, span);
        compile_pattern_bind(fn_expr.param, false);
    }
    compile_expr_impl(fn_expr.body, true);
    emit(OpCode::Return, 0, 0, span);
    auto proto = end_fn();

    Value fn_val{ValClosure{proto, {}}};
    uint32_t idx = chunk().add_constant(std::move(fn_val));
    emit(OpCode::MakeClosure, static_cast<int32_t>(idx), 0, span);
}

void Compiler::compile_match_expr(const ExprMatch& match_expr, const SourceSpan& span,
                                  bool tail_pos) {
    compile_expr_impl(match_expr.scrutinee, false);
    std::vector<size_t> end_jumps;

    size_t scrut_mark = current_->locals.size();
    uint32_t scrut_slot = alloc_local(Ident{"$match", {}, span});
    emit(OpCode::SetLocal, static_cast<int32_t>(scrut_slot), 0, span);

    for (const auto& arm : match_expr.arms) {
        size_t arm_mark = current_->locals.size();
        emit(OpCode::GetLocal, static_cast<int32_t>(scrut_slot), 0, arm.span);
        std::vector<size_t> fail_jumps;
        compile_pattern_match(arm.pat, fail_jumps);
        compile_expr_impl(arm.body, tail_pos);
        pop_locals_to(arm_mark, arm.span);
        end_jumps.push_back(emit_jump(OpCode::Jump, arm.span));

        for (size_t fj : fail_jumps) {
            patch_jump(fj);
        }
        pop_locals_to(arm_mark, arm.span);
    }

    emit(OpCode::MatchFail, 0, 0, span);
    for (size_t ej : end_jumps) {
        patch_jump(ej);
    }
    pop_locals_to(scrut_mark, span);
}

void Compiler::compile_quote_expr(const ExprQuote& quote_expr, const SourceSpan& span) {
    std::vector<ExprPtr> unquotes;

    std::function<PatternPtr(const PatternPtr&, int)> extract_pat;
    std::function<ExprPtr(const ExprPtr&, int)> extract_expr;

    extract_pat = [&](const PatternPtr& pat, int depth) -> PatternPtr {
        if (!pat) return nullptr;
        return std::visit(
            [&](const auto& n) -> PatternPtr {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, PatUnquote>) {
                    if (depth == 0) {
                        size_t idx = unquotes.size();
                        unquotes.push_back(n.expr);
                        return std::make_shared<Pattern>(Pattern{PatUnquoteSlot{idx}, pat->span});
                    }
                    return std::make_shared<Pattern>(
                        Pattern{PatUnquote{extract_expr(n.expr, depth - 1)}, pat->span});
                } else if constexpr (std::is_same_v<T, PatTuple>) {
                    std::vector<PatternPtr> elems;
                    for (const auto& e : n.elems) elems.push_back(extract_pat(e, depth));
                    return std::make_shared<Pattern>(Pattern{PatTuple{std::move(elems)}, pat->span});
                } else if constexpr (std::is_same_v<T, PatList>) {
                    std::vector<PatternPtr> elems;
                    for (const auto& e : n.elems) elems.push_back(extract_pat(e, depth));
                    return std::make_shared<Pattern>(Pattern{PatList{std::move(elems)}, pat->span});
                } else if constexpr (std::is_same_v<T, PatCons>) {
                    return std::make_shared<Pattern>(
                        Pattern{PatCons{extract_pat(n.head, depth), extract_pat(n.tail, depth)},
                                pat->span});
                } else if constexpr (std::is_same_v<T, PatCtor>) {
                    std::vector<PatternPtr> args;
                    for (const auto& a : n.args) args.push_back(extract_pat(a, depth));
                    return std::make_shared<Pattern>(
                        Pattern{PatCtor{n.ctor, std::move(args)}, pat->span});
                } else {
                    return pat;
                }
            },
            pat->node);
    };

    extract_expr = [&](const ExprPtr& e, int depth) -> ExprPtr {
        if (!e) return nullptr;
        return std::visit(
            [&](const auto& n) -> ExprPtr {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, ExprUnquote>) {
                    if (depth == 0) {
                        size_t idx = unquotes.size();
                        unquotes.push_back(n.expr);
                        return std::make_shared<Expr>(Expr{ExprUnquoteSlot{idx}, e->span});
                    }
                    return std::make_shared<Expr>(
                        Expr{ExprUnquote{extract_expr(n.expr, depth - 1)}, e->span});
                } else if constexpr (std::is_same_v<T, ExprQuote>) {
                    return std::make_shared<Expr>(
                        Expr{ExprQuote{extract_expr(n.body, depth + 1)}, e->span});
                } else if constexpr (std::is_same_v<T, ExprTuple>) {
                    std::vector<ExprPtr> elems;
                    for (const auto& el : n.elems) elems.push_back(extract_expr(el, depth));
                    return std::make_shared<Expr>(Expr{ExprTuple{std::move(elems)}, e->span});
                } else if constexpr (std::is_same_v<T, ExprList>) {
                    std::vector<ExprPtr> elems;
                    for (const auto& el : n.elems) elems.push_back(extract_expr(el, depth));
                    return std::make_shared<Expr>(Expr{ExprList{std::move(elems)}, e->span});
                } else if constexpr (std::is_same_v<T, ExprUnary>) {
                    return std::make_shared<Expr>(
                        Expr{ExprUnary{n.op, extract_expr(n.operand, depth)}, e->span});
                } else if constexpr (std::is_same_v<T, ExprBinary>) {
                    return std::make_shared<Expr>(
                        Expr{ExprBinary{n.op, extract_expr(n.left, depth), extract_expr(n.right, depth)},
                             e->span});
                } else if constexpr (std::is_same_v<T, ExprIf>) {
                    return std::make_shared<Expr>(
                        Expr{ExprIf{extract_expr(n.cond, depth), extract_expr(n.then_branch, depth),
                                    extract_expr(n.else_branch, depth)},
                             e->span});
                } else if constexpr (std::is_same_v<T, ExprLet>) {
                    return std::make_shared<Expr>(
                        Expr{ExprLet{n.is_rec, extract_pat(n.pat, depth), n.type_ann,
                                     extract_expr(n.value, depth), extract_expr(n.body, depth)},
                             e->span});
                } else if constexpr (std::is_same_v<T, ExprFn>) {
                    return std::make_shared<Expr>(
                        Expr{ExprFn{extract_pat(n.param, depth), n.param_type,
                                    extract_expr(n.body, depth)},
                             e->span});
                } else if constexpr (std::is_same_v<T, ExprApp>) {
                    return std::make_shared<Expr>(
                        Expr{ExprApp{extract_expr(n.fn, depth), extract_expr(n.arg, depth)}, e->span});
                } else if constexpr (std::is_same_v<T, ExprMacroCall>) {
                    std::vector<ExprPtr> args;
                    for (const auto& a : n.args) args.push_back(extract_expr(a, depth));
                    return std::make_shared<Expr>(
                        Expr{ExprMacroCall{n.name, std::move(args)}, e->span});
                } else if constexpr (std::is_same_v<T, ExprMatch>) {
                    std::vector<MatchArm> arms;
                    for (const auto& arm : n.arms) {
                        arms.push_back(MatchArm{extract_pat(arm.pat, depth),
                                                extract_expr(arm.body, depth), arm.span});
                    }
                    return std::make_shared<Expr>(
                        Expr{ExprMatch{extract_expr(n.scrutinee, depth), std::move(arms)}, e->span});
                } else if constexpr (std::is_same_v<T, ExprAnnot>) {
                    return std::make_shared<Expr>(
                        Expr{ExprAnnot{extract_expr(n.expr, depth), n.type}, e->span});
                } else {
                    return e;
                }
            },
            e->node);
    };

    ExprPtr templ = extract_expr(quote_expr.body, 0);
    for (const auto& u : unquotes) {
        compile_expr_impl(u, false);
    }
    uint32_t qidx = chunk().add_quote(std::move(templ), unquotes.size());
    emit(OpCode::MakeQuote, static_cast<int32_t>(qidx), static_cast<int32_t>(unquotes.size()), span);
}

}  // namespace cml
