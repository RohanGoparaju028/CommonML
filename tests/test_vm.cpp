#include "test_framework.hpp"
#include "cml/pipeline.hpp"

using namespace cml;

CML_TEST(vm_closures_and_currying) {
    Pipeline p;
    EvalResult res = p.eval_source("closures.cml", R"(
        let make_adder x = fn y -> x + y
        let add10 = make_adder 10
        add10 32
    )");
    CML_ASSERT_EQ(value_to_string(res.value), std::string("42"));
}

CML_TEST(vm_recursion_and_list_patterns) {
    Pipeline p;
    EvalResult res = p.eval_source("lists.cml", R"(
        let rec map f xs =
          match xs with
          | [] -> []
          | h :: t -> (f h) :: (map f t)

        let rec sum xs =
          match xs with
          | [] -> 0
          | h :: t -> h + sum t

        [1, 2, 3, 4]
        |> map (fn x -> x * x)
        |> sum
    )");
    CML_ASSERT_EQ(value_to_string(res.value), std::string("30"));
}

CML_TEST(vm_adt_tree_and_tuples) {
    Pipeline p;
    EvalResult res = p.eval_source("adt.cml", R"(
        type Option a = None | Some a
        let safe_div a b =
          if b == 0 then None else Some (a / b)

        let pair = (safe_div 20 4, safe_div 5 0)
        match pair with
        | (Some v, None) -> v * 2
        | _ -> -1
    )");
    CML_ASSERT_EQ(value_to_string(res.value), std::string("10"));
}

CML_TEST(vm_short_circuit_and_tail_recursion) {
    Pipeline p;
    EvalResult res = p.eval_source("tail.cml", R"(
        let rec count_down n acc =
          if n == 0 then acc else count_down (n - 1) (acc + 1)

        let safe = (false && (1 / 0 == 0)) || (true || (1 / 0 == 0))
        if safe then count_down 20000 0 else -1
    )");
    CML_ASSERT_EQ(value_to_string(res.value), std::string("20000"));
}
