#include "cml/pipeline.hpp"
#include "cml/repl.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options] [file.cml]\n"
              << "Options:\n"
              << "  -e <expr>       Evaluate inline source\n"
              << "  --tokens        Dump lexer tokens\n"
              << "  --ast           Dump parsed AST\n"
              << "  --expanded      Dump macro-expanded AST\n"
              << "  --types         Dump inferred types\n"
              << "  --bc            Dump compiled bytecode\n"
              << "  --check         Typecheck only without executing\n"
              << "  -h, --help      Show this help message\n";
}

int main(int argc, char** argv) {
    cml::EvalOptions opts;
    std::string inline_code;
    bool has_inline = false;
    std::string filename;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--tokens") {
            opts.dump_tokens = true;
        } else if (arg == "--ast") {
            opts.dump_ast = true;
        } else if (arg == "--expanded") {
            opts.dump_expanded = true;
        } else if (arg == "--types") {
            opts.dump_types = true;
        } else if (arg == "--bc") {
            opts.dump_bytecode = true;
        } else if (arg == "--check") {
            opts.typecheck_only = true;
        } else if (arg == "-e") {
            if (i + 1 >= argc) {
                std::cerr << "error: -e requires an argument\n";
                return 1;
            }
            inline_code = argv[++i];
            has_inline = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            return 1;
        } else {
            filename = arg;
        }
    }

    if (!has_inline && filename.empty()) {
        cml::Repl repl;
        return repl.run(std::cin, std::cout, std::cerr);
    }

    std::string source_name = has_inline ? "<inline>" : filename;
    std::string code = inline_code;
    if (!has_inline) {
        std::ifstream ifs(filename);
        if (!ifs) {
            std::cerr << "error: could not open file '" << filename << "'\n";
            return 1;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        code = ss.str();
    }

    cml::Pipeline pipeline;
    try {
        cml::EvalResult res = pipeline.eval_source(source_name, code, opts);
        if (!res.debug_output.empty()) {
            std::cout << res.debug_output;
        }
        if (!opts.typecheck_only && has_inline) {
            std::cout << cml::value_to_string(res.value) << "\n";
        }
    } catch (const cml::CompileError& e) {
        cml::SourceFile sf(source_name, code);
        std::cerr << e.diagnostic().format(&sf);
        return 1;
    }
    return 0;
}
