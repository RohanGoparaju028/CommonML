#include "test_framework.hpp"
#include "cml/pipeline.hpp"

using namespace cml;

CML_TEST(macro_basic_quote_and_unquote) {
    Pipeline p;
    EvalResult res = p.eval_source("macro.cml", R"(
        macro swap_if cond a b =
          quote { if $cond then $b else $a }
        swap_if!(true, 10, 20)
    )");
    CML_ASSERT_EQ(value_to_string(res.value), std::string("20"));
}

CML_TEST(macro_hygiene_avoids_capture) {
    Pipeline p;
    EvalResult res = p.eval_source("hygiene.cml", R"(
        macro with_temp v body =
          quote {
            let x = $v + 1 in
            $body + x
          }
        let x = 100
        with_temp!(5, x)
    )");
    // Caller's `x` (100) must NOT be captured by macro's `let x = 5 + 1` (6).
    CML_ASSERT_EQ(value_to_string(res.value), std::string("106"));
}

CML_TEST(macro_nested_expansion) {
    Pipeline p;
    EvalResult res = p.eval_source("nested.cml", R"(
        macro double_expr e = quote { ($e) + ($e) }
        macro quad_expr e = quote { double_expr!(double_expr!($e)) }
        quad_expr!(3)
    )");
    CML_ASSERT_EQ(value_to_string(res.value), std::string("12"));
}
