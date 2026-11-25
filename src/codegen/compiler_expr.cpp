#include "cml/compiler.hpp"

#include <functional>

namespace cml {

std::shared_ptr<FunctionProto> Compiler::compile_program(const Program& prog) {
    begin_fn("<script>", 0);
    if (prog.stmts.empty()) {
        uint32_t idx = chunk().add_constant(Value::unit());
        emit(OpCode::Const, static_cast<int32_t>(idx));
        emit(OpCode::Return);
        return end_fn();
    }
    for (size_t i = 0; i < prog.stmts.size(); ++i) {
        bool is_last = (i + 1 == prog.stmts.size());
        compile_stmt_impl(prog.stmts[i], is_last);
    }
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
            if constexpr (std::is_same_v<T, StmtTypeDecl> || std::is_same_v<T, StmtMacro>) {
                if (keep_value) {
                    uint32_t idx = chunk().add_constant(Value::unit());
                    emit(OpCode::Const, static_cast<int32_t>(idx), 0, stmt->span);
                }
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
                compile_expr_impl(n.operand, false);
                emit(n.op == UnaryOp::NegInt ? OpCode::NegInt : OpCode::NegFloat, 0, 0, expr->span);
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
