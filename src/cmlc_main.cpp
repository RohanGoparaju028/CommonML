#include "cml/pipeline.hpp"
#include "cml/toolchain.hpp"

#include <iostream>
#include <string>

#ifndef CML_VERSION_STRING
#define CML_VERSION_STRING "0.1.0"
#endif

#ifndef CML_COMMONML_BIN
#define CML_COMMONML_BIN ""
#endif

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options] <file.cml>\n"
              << "Options:\n"
              << "  -o <output>     Output executable or .cmlbc path\n"
              << "  -c, --emit-bc   Write raw .cmlbc bytecode without executable header\n"
              << "  --tokens        Dump lexer tokens\n"
              << "  --ast           Dump parsed AST\n"
              << "  --expanded      Dump macro-expanded AST\n"
              << "  --types         Dump inferred types\n"
              << "  --bc            Disassemble compiled bytecode\n"
              << "  -v, --version   Show version\n"
              << "  -h, --help      Show help message\n";
}

int main(int argc, char** argv) {
    cml::set_executable_path(argc > 0 ? argv[0] : "");

    cml::CompileOutputOptions co;
    co.fallback_runtime = cml::resolve_runtime_binary(argc > 0 ? argv[0] : "", CML_COMMONML_BIN);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "cmlc (CommonML) " << CML_VERSION_STRING << "\n";
            return 0;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "error: -o requires an output path\n";
                return 1;
            }
            co.output_file = argv[++i];
        } else if (arg == "-c" || arg == "--emit-bc") {
            co.emit_bc_only = true;
        } else if (arg == "--tokens") {
            co.eval_opts.dump_tokens = true;
        } else if (arg == "--ast") {
            co.eval_opts.dump_ast = true;
        } else if (arg == "--expanded") {
            co.eval_opts.dump_expanded = true;
        } else if (arg == "--types") {
            co.eval_opts.dump_types = true;
        } else if (arg == "--bc") {
            co.eval_opts.dump_bytecode = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            return 1;
        } else if (co.input_file.empty()) {
            co.input_file = arg;
        } else {
            std::cerr << "error: unexpected extra argument '" << arg << "'\n";
            return 1;
        }
    }

    if (co.input_file.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    return cml::compile_file_to_output(co, std::cout, std::cerr);
}
