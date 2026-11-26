#pragma once

#include "cml/ast.hpp"
#include "cml/lexer.hpp"

#include <vector>

namespace cml {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Program parse_program();
    ExprPtr parse_single_expr();

    struct Param {
        PatternPtr pat;
        std::optional<TypeExprPtr> type;
        SourceSpan span;
    };

private:
    const Token& peek(size_t offset = 0) const;
    const Token& previous() const;
    bool at_end() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token& advance();
    const Token& expect(TokenKind kind, const std::string& msg);
    [[noreturn]] void error(const Token& tok, const std::string& msg) const;

    StmtPtr parse_stmt(bool in_block = false);
    StmtPtr parse_type_decl();
    StmtPtr parse_struct_body(const SourceSpan& start_span, const Ident& struct_name,
                              std::vector<Ident> type_params, size_t kw_col = 0);
    StmtPtr parse_let_stmt(bool in_block = false);
    StmtPtr parse_var_stmt();
    StmtPtr parse_macro_stmt();
    StmtPtr parse_assign_or_expr();

    TypeExprPtr parse_type();
    TypeExprPtr parse_type_tuple();
    TypeExprPtr parse_type_app();
    TypeExprPtr parse_type_atom();

    PatternPtr parse_pattern();
    PatternPtr parse_pat_cons();
    PatternPtr parse_pat_app();
    PatternPtr parse_pat_atom();
    bool starts_pat_atom() const;

    Param parse_param();

    ExprPtr parse_expr();
    ExprPtr parse_block(size_t min_indent_col = 0);
    ExprPtr parse_block_or_expr(size_t min_indent_col = 0);
    ExprPtr parse_seq_expr();
    ExprPtr parse_let_expr();
    ExprPtr parse_func_expr();
    ExprPtr parse_if_expr();
    ExprPtr parse_match_expr();
    ExprPtr parse_while_expr();
    ExprPtr parse_for_expr();
    ExprPtr parse_binary(int min_prec);
    ExprPtr parse_unary();
    ExprPtr parse_app();
    ExprPtr parse_postfix();
    ExprPtr parse_atom();
    bool starts_atom() const;
    bool starts_indented_block() const;
    void skip_stmt_seps();

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    int quote_depth_ = 0;
    size_t top_stmt_col_ = 0;
    int expr_delim_depth_ = 0;
    bool stop_at_pipe_ = false;
    int match_arm_delim_depth_ = 0;
};

}  // namespace cml
