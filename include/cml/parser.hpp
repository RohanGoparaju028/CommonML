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

    StmtPtr parse_stmt();
    StmtPtr parse_type_decl();
    StmtPtr parse_let_stmt();
    StmtPtr parse_macro_stmt();

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
    ExprPtr parse_seq_expr();
    ExprPtr parse_let_expr();
    ExprPtr parse_fn_expr();
    ExprPtr parse_if_expr();
    ExprPtr parse_match_expr();
    ExprPtr parse_binary(int min_prec);
    ExprPtr parse_unary();
    ExprPtr parse_app();
    ExprPtr parse_atom();
    bool starts_atom() const;

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    int quote_depth_ = 0;
    size_t top_stmt_col_ = 0;
    int expr_delim_depth_ = 0;
};

}  // namespace cml
