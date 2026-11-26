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
    src/driver/debugger.cpp
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
if(WIN32)
    target_link_libraries(commonml_core PUBLIC ws2_32)
endif()

add_executable(commonml src/main.cpp)
target_link_libraries(commonml PRIVATE commonml_core)

add_executable(cml src/main.cpp)
target_link_libraries(cml PRIVATE commonml_core)

add_executable(cmlc src/cmlc_main.cpp)
target_link_libraries(cmlc PRIVATE commonml_core)

foreach(tgt IN ITEMS commonml cml cmlc)
    target_compile_definitions(${tgt} PRIVATE
        CML_COMMONML_BIN="$<TARGET_FILE:commonml>"
    )
endforeach()

install(TARGETS commonml cml cmlc RUNTIME DESTINATION bin COMPONENT commonml)
install(DIRECTORY stdlib/ DESTINATION share/commonml/stdlib COMPONENT commonml FILES_MATCHING PATTERN "*.cml")
install(DIRECTORY bootstrap/ DESTINATION share/commonml/bootstrap COMPONENT commonml FILES_MATCHING PATTERN "*.cml")
install(DIRECTORY editors/vscode/ DESTINATION share/commonml/editors/vscode COMPONENT commonml)
install(DIRECTORY editors/vim/ DESTINATION share/commonml/editors/vim COMPONENT commonml)
install(DIRECTORY editors/emacs/ DESTINATION share/commonml/editors/emacs COMPONENT commonml)
install(FILES README.md DESTINATION share/doc/commonml COMPONENT commonml)
install(FILES packaging/linux/commonml.xml DESTINATION share/mime/packages COMPONENT commonml)
install(FILES packaging/linux/commonml.desktop DESTINATION share/applications COMPONENT commonml)
install(FILES packaging/macos/Info.plist DESTINATION share/commonml/macos COMPONENT commonml)
install(FILES packaging/windows/register_cml.cmd DESTINATION share/commonml/windows COMPONENT commonml)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_CURRENT_BINARY_DIR}/README.txt" COPYONLY)

set(CPACK_PACKAGE_NAME "commonml")
set(CPACK_PACKAGE_VENDOR "CommonML Project")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CommonML functional systems language compiler, VM, debugger, and package manager")
set(CPACK_PACKAGE_CONTACT "Rohan Goparaju <goparajurohan41@gmail.com>")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_BINARY_DIR}/README.txt")

if(WIN32)
    set(CPACK_GENERATOR "WIX;ZIP")
    set(CPACK_WIX_UPGRADE_GUID "6B7A9012-7E5A-4B48-9B9C-2026C0AA0101")
    set(CPACK_WIX_PRODUCT_GUID "9C8F31A0-5A21-48A0-A3E4-2026C0AA0101")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "CommonML")
elseif(APPLE)
    set(CPACK_GENERATOR "productbuild;TGZ")
    set(CPACK_PRODUCTBUILD_IDENTIFIER "org.commonml")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Rohan Goparaju <goparajurohan41@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
endif()
include(CPack)
cpack_add_component(commonml DISPLAY_NAME "CommonML Toolchain" REQUIRED)

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
