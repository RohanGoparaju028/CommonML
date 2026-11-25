#include "test_framework.hpp"
#include "cml/lexer.hpp"
#include "cml/parser.hpp"
#include "cml/typecheck.hpp"

using namespace cml;

static std::vector<std::string> check_src(const std::string& code) {
    SourceFile sf("test.cml", code);
    Lexer lex(sf);
    Parser parser(lex.tokenize());
    Program prog = parser.parse_program();
    TypeChecker tc;
    return tc.check_program(prog);
}

CML_TEST(typecheck_let_polymorphism) {
    auto types = check_src(R"(
        let id x = x
        let a = id 42
        let b = id "hello"
    )");
    CML_ASSERT_EQ(types[0], std::string("id : 'a -> 'a"));
    CML_ASSERT_EQ(types[1], std::string("a : Int"));
    CML_ASSERT_EQ(types[2], std::string("b : String"));
}

CML_TEST(typecheck_recursive_and_adt) {
    auto types = check_src(R"(
        type Tree a = Leaf | Node (Tree a) a (Tree a)
        let rec sum_tree t =
          match t with
          | Leaf -> 0
          | Node l v r -> sum_tree l + v + sum_tree r
    )");
    CML_ASSERT_EQ(types[1], std::string("sum_tree : Tree Int -> Int"));
}

CML_TEST(typecheck_rejects_mismatch_and_occurs) {
    CML_ASSERT_THROWS(check_src("let bad = 1 + true"), CompileError);
    CML_ASSERT_THROWS(check_src("let inf = fn x -> x x"), CompileError);
}
