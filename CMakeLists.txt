cmake_minimum_required(VERSION 3.16)
project(commonml VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(commonml_core STATIC
    src/source.cpp
    src/lexer.cpp
    src/ast.cpp
    src/parser.cpp
    src/types/type.cpp
    src/types/unify.cpp
    src/types/typecheck_env.cpp
    src/types/typecheck_pat.cpp
    src/types/typecheck_expr.cpp
    src/runtime/value.cpp
    src/runtime/builtins.cpp
    src/bytecode/bytecode.cpp
    src/codegen/compiler_state.cpp
    src/codegen/compiler_pat.cpp
    src/codegen/compiler_expr.cpp
    src/vm/vm.cpp
    src/vm/vm_ops.cpp
    src/macro/macro_expander.cpp
    src/driver/pipeline.cpp
    src/driver/repl.cpp
)

target_include_directories(commonml_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(commonml src/main.cpp)
target_link_libraries(commonml PRIVATE commonml_core)

enable_testing()
add_executable(commonml_tests
    tests/test_main.cpp
    tests/test_lexer_parser.cpp
    tests/test_typecheck.cpp
    tests/test_macros.cpp
    tests/test_vm.cpp
)
target_link_libraries(commonml_tests PRIVATE commonml_core)
add_test(NAME commonml_unit_tests COMMAND commonml_tests)
