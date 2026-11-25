#include "cml/pipeline.hpp"
#include "cml/lexer.hpp"
#include "cml/parser.hpp"

#include <sstream>

namespace cml {

Pipeline::Pipeline() {
    vm_.register_ctors(typechecker_.ctors());
}

EvalResult Pipeline::eval_source(const std::string& filename, const std::string& code,
                                 const EvalOptions& opts) {
    EvalResult res;
    std::ostringstream dbg;

    SourceFile src(filename, code);
    Lexer lexer(src);
    std::vector<Token> tokens = lexer.tokenize();

    if (opts.dump_tokens) {
        dbg << "== Tokens ==\n";
        for (const auto& t : tokens) {
            dbg << t.span.line << ":" << t.span.column << " " << token_kind_name(t.kind)
                << " '" << t.lexeme << "'\n";
        }
    }

    Parser parser(std::move(tokens));
    Program prog = parser.parse_program();

    if (opts.dump_ast) {
        dbg << "== AST ==\n";
        for (const auto& s : prog.stmts) {
            dbg << stmt_to_string(s) << "\n";
        }
    }

    Program expanded = expander_.expand_program(prog);
    if (opts.dump_expanded) {
        dbg << "== Expanded AST ==\n";
        for (const auto& s : expanded.stmts) {
            dbg << stmt_to_string(s) << "\n";
        }
    }

    res.stmt_types = typechecker_.check_program(expanded);
    if (!res.stmt_types.empty()) {
        res.type_str = res.stmt_types.back();
    }
    if (opts.dump_types) {
        dbg << "== Types ==\n";
        for (const auto& t : res.stmt_types) {
            dbg << t << "\n";
        }
    }

    vm_.register_ctors(typechecker_.ctors());

    Compiler compiler(&typechecker_.ctors());
    auto proto = compiler.compile_program(expanded);

    if (opts.dump_bytecode) {
        dbg << disassemble_chunk(proto->chunk, filename);
    }

    if (!opts.typecheck_only) {
        res.value = vm_.run(proto);
    }

    res.debug_output = dbg.str();
    return res;
}

EvalResult Pipeline::eval_expr(const std::string& filename, const std::string& code,
                               const EvalOptions& opts) {
    EvalResult res;
    std::ostringstream dbg;

    SourceFile src(filename, code);
    Lexer lexer(src);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(std::move(tokens));
    ExprPtr expr = parser.parse_single_expr();
    ExprPtr expanded = expander_.expand_expr(expr);

    TypePtr ty = typechecker_.infer_expr(expanded);
    res.type_str = type_to_string(ty);

    Compiler compiler(&typechecker_.ctors());
    auto proto = compiler.compile_expr(expanded);
    if (opts.dump_bytecode) {
        dbg << disassemble_chunk(proto->chunk, filename);
    }
    if (!opts.typecheck_only) {
        res.value = vm_.run(proto);
    }
    res.debug_output = dbg.str();
    return res;
}

}  // namespace cml
