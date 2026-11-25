#include "test_framework.hpp"
#include "cml/lexer.hpp"
#include "cml/parser.hpp"

using namespace cml;

CML_TEST(lexer_tokens_and_comments) {
    SourceFile sf("test.cml", R"(
        -- line comment
        (* nested (* block *) comment *)
        let rec f x = x + 1_000
    )");
    Lexer lex(sf);
    auto toks = lex.tokenize();
    CML_ASSERT(toks.size() >= 8);
    CML_ASSERT_EQ(static_cast<int>(toks[0].kind), static_cast<int>(TokenKind::KwLet));
    CML_ASSERT_EQ(static_cast<int>(toks[1].kind), static_cast<int>(TokenKind::KwRec));
    CML_ASSERT_EQ(toks[2].lexeme, std::string("f"));
    CML_ASSERT_EQ(toks[7].int_val, int64_t{1000});
}

CML_TEST(parser_precedence_and_pipe) {
    SourceFile sf("test.cml", "1 + 2 * 3 |> f");
    Lexer lex(sf);
    Parser parser(lex.tokenize());
    auto expr = parser.parse_single_expr();
    auto* pipe = std::get_if<ExprBinary>(&expr->node);
    CML_ASSERT(pipe != nullptr);
    CML_ASSERT_EQ(static_cast<int>(pipe->op), static_cast<int>(BinaryOp::Pipe));
    auto* add = std::get_if<ExprBinary>(&pipe->left->node);
    CML_ASSERT(add != nullptr);
    CML_ASSERT_EQ(static_cast<int>(add->op), static_cast<int>(BinaryOp::AddInt));
}

CML_TEST(parser_adt_and_match) {
    SourceFile sf("test.cml", R"(
        type Option a = None | Some a
        let unwrap_or d opt =
          match opt with
          | None -> d
          | Some x -> x
    )");
    Lexer lex(sf);
    Parser parser(lex.tokenize());
    Program prog = parser.parse_program();
    CML_ASSERT_EQ(prog.stmts.size(), size_t{2});
    CML_ASSERT(std::holds_alternative<StmtTypeDecl>(prog.stmts[0]->node));
    CML_ASSERT(std::holds_alternative<StmtLet>(prog.stmts[1]->node));
}
