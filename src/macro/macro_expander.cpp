#include "cml/macro_expander.hpp"
#include "cml/compiler.hpp"

namespace cml {

static constexpr size_t kMaxMacroDepth = 256;

MacroExpander::MacroExpander() = default;

bool MacroExpander::has_macro(const std::string& name) const {
    return macros_.lookup(Ident{name, {}, {}}) != nullptr;
}

Program MacroExpander::expand_program(const Program& prog) {
    Program out;
    out.stmts.reserve(prog.stmts.size());
    for (const auto& stmt : prog.stmts) {
        out.stmts.push_back(expand_stmt(stmt));
    }
    return out;
}

void MacroExpander::register_macro(const StmtMacro& macro, const SourceSpan& span) {
    (void)span;
    Compiler compiler;
    auto proto = compiler.compile_expr(macro.body, macro.params);
    macros_.push(macro.name, MacroDef{macro.name, macro.params, macro.body, std::move(proto)});
}

StmtPtr MacroExpander::expand_stmt(const StmtPtr& stmt) {
    if (!stmt) return nullptr;
    return std::visit(
        [&](const auto& n) -> StmtPtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, StmtMacro>) {
                register_macro(n, stmt->span);
                return stmt;
            } else if constexpr (std::is_same_v<T, StmtLet>) {
                return std::make_shared<Stmt>(
                    Stmt{StmtLet{n.is_rec, expand_pattern(n.pat, 0), n.type_ann,
                                 expand_expr(n.value, 0)},
                         stmt->span});
            } else if constexpr (std::is_same_v<T, StmtVar>) {
                return std::make_shared<Stmt>(
                    Stmt{StmtVar{n.name, n.type_ann, expand_expr(n.value, 0)}, stmt->span});
            } else if constexpr (std::is_same_v<T, StmtExpr>) {
                return std::make_shared<Stmt>(
                    Stmt{StmtExpr{expand_expr(n.expr, 0)}, stmt->span});
            } else {
                return stmt;
            }
        },
        stmt->node);
}

PatternPtr MacroExpander::expand_pattern(const PatternPtr& pat, size_t depth) {
    if (!pat) return nullptr;
    return std::visit(
        [&](const auto& n) -> PatternPtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, PatTuple>) {
                std::vector<PatternPtr> elems;
                for (const auto& e : n.elems) elems.push_back(expand_pattern(e, depth));
                return std::make_shared<Pattern>(Pattern{PatTuple{std::move(elems)}, pat->span});
            } else if constexpr (std::is_same_v<T, PatList>) {
                std::vector<PatternPtr> elems;
                for (const auto& e : n.elems) elems.push_back(expand_pattern(e, depth));
                return std::make_shared<Pattern>(Pattern{PatList{std::move(elems)}, pat->span});
            } else if constexpr (std::is_same_v<T, PatCons>) {
                return std::make_shared<Pattern>(
                    Pattern{PatCons{expand_pattern(n.head, depth), expand_pattern(n.tail, depth)},
                            pat->span});
            } else if constexpr (std::is_same_v<T, PatCtor>) {
                std::vector<PatternPtr> args;
                for (const auto& a : n.args) args.push_back(expand_pattern(a, depth));
                return std::make_shared<Pattern>(
                    Pattern{PatCtor{n.ctor, std::move(args)}, pat->span});
            } else {
                return pat;
            }
        },
        pat->node);
}

ExprPtr MacroExpander::invoke_macro(const MacroDef& def, const ExprMacroCall& call, size_t depth) {
    if (depth >= kMaxMacroDepth) {
        throw CompileError(
            Diagnostic{"macro", "maximum macro expansion depth exceeded", call.name.span});
    }
    if (call.args.size() != def.params.size()) {
        throw CompileError(Diagnostic{"macro",
                                      "macro '" + def.name.name + "' expects " +
                                          std::to_string(def.params.size()) + " argument(s), got " +
                                          std::to_string(call.args.size()),
                                      call.name.span});
    }

    ScopeId call_scope = next_scope_++;
    std::vector<Value> vm_args;
    vm_args.reserve(call.args.size());
    for (const auto& arg : call.args) {
        ExprPtr flipped_arg = flip_scope_expr(arg, call_scope);
        vm_args.push_back(Value::make_ast_expr(std::move(flipped_arg)));
    }

    Value result_val = macro_vm_.call_value(Value{ValClosure{def.compiled_fn, {}}}, vm_args,
                                            call.name.span);
    auto* ast_res = std::get_if<ValAstExpr>(&result_val.node);
    if (!ast_res || !ast_res->expr) {
        throw CompileError(Diagnostic{
            "macro", "macro '" + def.name.name + "' did not return an AST expression",
            call.name.span});
    }

    ExprPtr expanded = flip_scope_expr(ast_res->expr, call_scope);
    return expand_expr(expanded, depth + 1);
}

ExprPtr MacroExpander::expand_expr(const ExprPtr& expr, size_t depth) {
    if (!expr) return nullptr;
    return std::visit(
        [&](const auto& n) -> ExprPtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, ExprMacroCall>) {
                const MacroDef* def = macros_.lookup(n.name);
                if (!def) {
                    throw CompileError(
                        Diagnostic{"macro", "unknown macro '" + n.name.name + "'", expr->span});
                }
                return invoke_macro(*def, n, depth);
            } else if constexpr (std::is_same_v<T, ExprTuple>) {
                std::vector<ExprPtr> elems;
                for (const auto& e : n.elems) elems.push_back(expand_expr(e, depth));
                return std::make_shared<Expr>(Expr{ExprTuple{std::move(elems)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprList>) {
                std::vector<ExprPtr> elems;
                for (const auto& e : n.elems) elems.push_back(expand_expr(e, depth));
                return std::make_shared<Expr>(Expr{ExprList{std::move(elems)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprUnary>) {
                return std::make_shared<Expr>(
                    Expr{ExprUnary{n.op, expand_expr(n.operand, depth)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprBinary>) {
                return std::make_shared<Expr>(
                    Expr{ExprBinary{n.op, expand_expr(n.left, depth), expand_expr(n.right, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprIf>) {
                return std::make_shared<Expr>(
                    Expr{ExprIf{expand_expr(n.cond, depth), expand_expr(n.then_branch, depth),
                                expand_expr(n.else_branch, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprLet>) {
                return std::make_shared<Expr>(
                    Expr{ExprLet{n.is_rec, expand_pattern(n.pat, depth), n.type_ann,
                                 expand_expr(n.value, depth), expand_expr(n.body, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprFn>) {
                return std::make_shared<Expr>(
                    Expr{ExprFn{expand_pattern(n.param, depth), n.param_type,
                                expand_expr(n.body, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprApp>) {
                return std::make_shared<Expr>(
                    Expr{ExprApp{expand_expr(n.fn, depth), expand_expr(n.arg, depth)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                std::vector<MatchArm> arms;
                for (const auto& arm : n.arms) {
                    arms.push_back(MatchArm{expand_pattern(arm.pat, depth),
                                            expand_expr(arm.body, depth), arm.span});
                }
                return std::make_shared<Expr>(
                    Expr{ExprMatch{expand_expr(n.scrutinee, depth), std::move(arms)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprAnnot>) {
                return std::make_shared<Expr>(
                    Expr{ExprAnnot{expand_expr(n.expr, depth), n.type}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprStructLit>) {
                std::vector<std::pair<Ident, ExprPtr>> fs;
                for (const auto& [fn, fv] : n.fields) fs.emplace_back(fn, expand_expr(fv, depth));
                return std::make_shared<Expr>(
                    Expr{ExprStructLit{n.type_name, std::move(fs)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprFieldGet>) {
                return std::make_shared<Expr>(
                    Expr{ExprFieldGet{expand_expr(n.obj, depth), n.field}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprFieldSet>) {
                return std::make_shared<Expr>(
                    Expr{ExprFieldSet{expand_expr(n.obj, depth), n.field,
                                      expand_expr(n.value, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprNew>) {
                return std::make_shared<Expr>(
                    Expr{ExprNew{expand_expr(n.value, depth)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprClose>) {
                return std::make_shared<Expr>(
                    Expr{ExprClose{expand_expr(n.ptr, depth)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprDefer>) {
                return std::make_shared<Expr>(
                    Expr{ExprDefer{expand_expr(n.action, depth)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprDerefSet>) {
                return std::make_shared<Expr>(
                    Expr{ExprDerefSet{expand_expr(n.ptr, depth), expand_expr(n.value, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprPtrFieldGet>) {
                return std::make_shared<Expr>(
                    Expr{ExprPtrFieldGet{expand_expr(n.ptr, depth), n.field}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprPtrFieldSet>) {
                return std::make_shared<Expr>(
                    Expr{ExprPtrFieldSet{expand_expr(n.ptr, depth), n.field,
                                         expand_expr(n.value, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprAssign>) {
                return std::make_shared<Expr>(
                    Expr{ExprAssign{n.target, expand_expr(n.value, depth)}, expr->span});
            } else if constexpr (std::is_same_v<T, ExprWhile>) {
                return std::make_shared<Expr>(
                    Expr{ExprWhile{expand_expr(n.cond, depth), expand_expr(n.body, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprForEach>) {
                return std::make_shared<Expr>(
                    Expr{ExprForEach{expand_pattern(n.pat, depth), expand_expr(n.iterable, depth),
                                     expand_expr(n.body, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprFor>) {
                std::optional<StmtPtr> init_s;
                if (n.init) init_s = expand_stmt(*n.init);
                std::optional<ExprPtr> step_e;
                if (n.step) step_e = expand_expr(*n.step, depth);
                return std::make_shared<Expr>(
                    Expr{ExprFor{init_s, expand_expr(n.cond, depth), step_e,
                                 expand_expr(n.body, depth)},
                         expr->span});
            } else if constexpr (std::is_same_v<T, ExprBlock>) {
                std::vector<StmtPtr> out_s;
                for (const auto& s : n.stmts) out_s.push_back(expand_stmt(s));
                return std::make_shared<Expr>(Expr{ExprBlock{std::move(out_s)}, expr->span});
            } else {
                return expr;
            }
        },
        expr->node);
}

}  // namespace cml
