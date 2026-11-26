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
    src/runtime/builtins_collections.cpp
    src/runtime/builtins_io.cpp
    src/runtime/builtins_net.cpp
    src/bytecode/bytecode.cpp
    src/codegen/compiler_state.cpp
    src/codegen/compiler_pat.cpp
    src/codegen/compiler_expr.cpp
    src/vm/vm.cpp
    src/vm/vm_ops.cpp
    src/macro/macro_expander.cpp
    src/driver/pipeline.cpp
    src/driver/repl.cpp
    src/driver/highlight.cpp
    src/driver/toolchain.cpp
    src/pkg/package.cpp
)

target_include_directories(commonml_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(commonml_core PUBLIC
    CML_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
    CML_VERSION_STRING="${PROJECT_VERSION}"
)

add_executable(commonml src/main.cpp)
target_link_libraries(commonml PRIVATE commonml_core)

add_executable(cml src/main.cpp)
target_link_libraries(cml PRIVATE commonml_core)

add_executable(cmlc src/cmlc_main.cpp)
target_link_libraries(cmlc PRIVATE commonml_core)
target_compile_definitions(cmlc PRIVATE
    CML_COMMONML_BIN="$<TARGET_FILE:commonml>"
)
target_compile_definitions(cml PRIVATE
    CML_COMMONML_BIN="$<TARGET_FILE:commonml>"
)
target_compile_definitions(commonml PRIVATE
    CML_COMMONML_BIN="$<TARGET_FILE:commonml>"
)

install(TARGETS commonml cml cmlc RUNTIME DESTINATION bin)
install(DIRECTORY stdlib/ DESTINATION share/commonml/stdlib FILES_MATCHING PATTERN "*.cml")
install(DIRECTORY bootstrap/ DESTINATION share/commonml/bootstrap FILES_MATCHING PATTERN "*.cml")
install(DIRECTORY editors/vscode/ DESTINATION share/commonml/editors/vscode)
install(FILES README.md DESTINATION share/doc/commonml)

set(CPACK_PACKAGE_NAME "commonml")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CommonML functional language compiler, VM, and package manager")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)

enable_testing()
add_executable(commonml_tests
    tests/test_main.cpp
    tests/test_lexer_parser.cpp
    tests/test_typecheck.cpp
    tests/test_macros.cpp
    tests/test_vm.cpp
    tests/test_platform.cpp
)
target_link_libraries(commonml_tests PRIVATE commonml_core)
add_test(NAME commonml_unit_tests COMMAND commonml_tests)
