#include "cml/builtins.hpp"
#include "cml/bytecode.hpp"
#include "cml/highlight.hpp"
#include "cml/package.hpp"
#include "cml/pipeline.hpp"
#include "cml/repl.hpp"
#include "cml/toolchain.hpp"
#include "cml/vm.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#ifndef CML_VERSION_STRING
#define CML_VERSION_STRING "0.1.0"
#endif

#ifndef CML_COMMONML_BIN
#define CML_COMMONML_BIN ""
#endif

static void print_usage(const char* prog) {
    std::cout << "Usage:\n"
              << "  " << prog << " run [file.cml] [-- args...]       Run a file (like 'go run') or build.cml project\n"
              << "  " << prog << " build [file.cml] [-o out]           Compile file to executable or build build.cml\n"
              << "  " << prog << " new <lib|console|web> <name>        Create a new project\n"
              << "  " << prog << " init <lib|console|web>              Initialize build.cml in current dir\n"
              << "  " << prog << " add <name> --path <dir>             Add local dependency\n"
              << "  " << prog << " install | test | pkg list           Manage/test project packages\n"
              << "  " << prog << " [options] [file.cml] [-- args...]   Run file, inline code, or start REPL\n\n"
              << "Options:\n"
              << "  -e <expr>       Evaluate inline source\n"
              << "  --tokens        Dump lexer tokens\n"
              << "  --ast           Dump parsed AST\n"
              << "  --expanded      Dump macro-expanded AST\n"
              << "  --types         Dump inferred types\n"
              << "  --bc            Dump compiled bytecode\n"
              << "  --check         Typecheck only without executing\n"
              << "  --highlight     Print ANSI syntax-highlighted source\n"
              << "  -v, --version   Show version\n"
              << "  -h, --help      Show this help message\n";
}

static std::string find_project_root_or_fail() {
    std::string m = cml::PackageManager::find_manifest(".");
    if (m.empty()) {
        throw cml::CompileError(
            cml::Diagnostic{"pkg", "no build.cml found in current or parent directory", {}});
    }
    return fs::path(m).parent_path().string();
}

static void configure_optional_manifest(cml::Pipeline& pipeline, const std::string& path_hint) {
    std::string manifest = cml::PackageManager::find_manifest(path_hint);
    if (!manifest.empty()) {
        try {
            cml::PackageManager::configure_pipeline(pipeline,
                                                    fs::path(manifest).parent_path().string());
        } catch (const cml::CompileError&) {
        }
    }
}

static int run_single_file(const std::string& filename, const std::vector<std::string>& args,
                           const cml::EvalOptions& opts, bool print_non_unit) {
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cerr << "error: could not open file '" << filename << "'\n";
        return 1;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string code = ss.str();

    cml::set_cli_args(args);
    cml::Pipeline pipeline;
    configure_optional_manifest(pipeline, filename);

    try {
        if (fs::path(filename).extension() == ".cmlbc" || code.rfind("CMLBC ", 0) == 0 ||
            code.find("\nCMLBC ") != std::string::npos) {
            cml::BytecodeProgram bp = cml::deserialize_bytecode(code);
            cml::VM vm;
            for (const auto& c : bp.ctors) {
                if (c.arity == 0) {
                    vm.set_global(c.name, cml::Value::make_adt(c.tag, c.name, {}));
                } else {
                    vm.set_global(c.name, cml::Value{cml::ValCtor{c.tag, c.name, c.arity, {}}});
                }
            }
            cml::Value res = vm.run(bp.entry);
            if (!std::holds_alternative<cml::ValUnit>(res.node)) {
                std::cout << cml::value_to_string(res) << "\n";
            }
            return 0;
        }

        cml::EvalResult res = pipeline.eval_source(filename, code, opts);
        if (!res.debug_output.empty()) {
            std::cout << res.debug_output;
        }
        if (print_non_unit && !opts.typecheck_only &&
            !std::holds_alternative<cml::ValUnit>(res.value.node)) {
            std::cout << cml::value_to_string(res.value) << "\n";
        }
    } catch (const cml::ProgramExit& e) {
        return e.code;
    } catch (const cml::CompileError& e) {
        cml::SourceFile sf(filename, code);
        std::cerr << e.diagnostic().format(&sf);
        return 1;
    }
    return 0;
}

static int handle_subcommand(int argc, char** argv) {
    std::string cmd = argv[1];
    try {
        if (cmd == "new") {
            if (argc < 4) {
                std::cerr << "usage: " << argv[0] << " new <lib|console|web> <name>\n";
                return 1;
            }
            cml::PackageManager::create_project(argv[3], argv[2],
                                                fs::path(argv[3]).filename().string());
            std::cout << "Created " << argv[2] << " package '" << argv[3] << "'\n";
            return 0;
        }
        if (cmd == "init") {
            std::string kind = (argc >= 3) ? argv[2] : "console";
            cml::PackageManager::init_project(".", kind);
            std::cout << "Initialized " << kind << " package in current directory\n";
            return 0;
        }
        if (cmd == "add") {
            std::string root = find_project_root_or_fail();
            if (argc == 3) {
                cml::PackageManager::add_dependency(root, "", argv[2]);
                std::cout << "Added dependency from '" << argv[2] << "'\n";
                return 0;
            }
            if (argc >= 5 && std::string(argv[3]) == "--path") {
                cml::PackageManager::add_dependency(root, argv[2], argv[4]);
                std::cout << "Added dependency '" << argv[2] << "' from '" << argv[4] << "'\n";
                return 0;
            }
            std::cerr << "usage: " << argv[0] << " add <name> --path <dir>\n";
            return 1;
        }
        if (cmd == "install") {
            return cml::PackageManager::cmd_install(find_project_root_or_fail(), std::cout);
        }
        if (cmd == "build") {
            if (argc >= 3 && std::string(argv[2]) != "--") {
                cml::CompileOutputOptions co;
                co.fallback_runtime = cml::resolve_runtime_binary(argv[0], CML_COMMONML_BIN);
                for (int i = 2; i < argc; ++i) {
                    std::string a = argv[i];
                    if (a == "-o" && i + 1 < argc) {
                        co.output_file = argv[++i];
                    } else if (a == "-c" || a == "--emit-bc") {
                        co.emit_bc_only = true;
                    } else if (a == "--bc") {
                        co.eval_opts.dump_bytecode = true;
                    } else if (co.input_file.empty()) {
                        co.input_file = a;
                    } else {
                        std::cerr << "error: unexpected argument '" << a << "'\n";
                        return 1;
                    }
                }
                if (!co.input_file.empty()) {
                    return cml::compile_file_to_output(co, std::cout, std::cerr);
                }
            }
            return cml::PackageManager::cmd_build(find_project_root_or_fail(), std::cout);
        }
        if (cmd == "run") {
            if (argc >= 3) {
                std::string target = argv[2];
                std::error_code ec;
                bool is_file = target != "--" &&
                               (fs::path(target).extension() == ".cml" ||
                                fs::path(target).extension() == ".cmlbc" ||
                                fs::is_regular_file(target, ec));
                if (is_file) {
                    std::vector<std::string> run_args;
                    for (int i = 3; i < argc; ++i) {
                        std::string a = argv[i];
                        if (a == "--" && i == 3) continue;
                        run_args.push_back(std::move(a));
                    }
                    return run_single_file(target, run_args, cml::EvalOptions{}, true);
                }
            }
            std::vector<std::string> run_args;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--" && i == 2) continue;
                run_args.push_back(std::move(a));
            }
            return cml::PackageManager::cmd_run(find_project_root_or_fail(), run_args, std::cout,
                                                std::cerr);
        }
        if (cmd == "test") {
            return cml::PackageManager::cmd_test(find_project_root_or_fail(), std::cout, std::cerr);
        }
        if (cmd == "pkg") {
            if (argc >= 3 && std::string(argv[2]) == "list") {
                return cml::PackageManager::cmd_list(find_project_root_or_fail(), std::cout);
            }
            std::cerr << "usage: " << argv[0] << " pkg list\n";
            return 1;
        }
    } catch (const cml::ProgramExit& e) {
        return e.code;
    } catch (const cml::CompileError& e) {
        std::cerr << e.diagnostic().format(nullptr);
        return 1;
    }
    return 1;
}

int main(int argc, char** argv) {
    cml::set_executable_path(argc > 0 ? argv[0] : "");

    if (argc >= 2) {
        std::string sub = argv[1];
        if (sub == "new" || sub == "init" || sub == "add" || sub == "install" ||
            sub == "build" || sub == "run" || sub == "test" || sub == "pkg") {
            return handle_subcommand(argc, argv);
        }
    }

    cml::EvalOptions opts;
    bool dump_highlight = false;
    bool run_bc = false;
    std::string inline_code;
    bool has_inline = false;
    std::string filename;
    std::vector<std::string> script_args;
    bool after_dashdash = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (after_dashdash) {
            script_args.push_back(arg);
            continue;
        }
        if (arg == "--") {
            after_dashdash = true;
            continue;
        }
        if (!filename.empty()) {
            script_args.push_back(arg);
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "CommonML " << CML_VERSION_STRING << "\n";
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
        } else if (arg == "--highlight") {
            dump_highlight = true;
        } else if (arg == "--run-bc") {
            run_bc = true;
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

    cml::set_cli_args(script_args);

    if (!has_inline && filename.empty()) {
        cml::Repl repl;
        return repl.run(std::cin, std::cout, std::cerr);
    }

    if (dump_highlight) {
        std::string code = inline_code;
        std::string source_name = "<inline>";
        if (!has_inline) {
            source_name = filename;
            std::ifstream ifs(filename);
            if (!ifs) {
                std::cerr << "error: could not open file '" << filename << "'\n";
                return 1;
            }
            std::ostringstream ss;
            ss << ifs.rdbuf();
            code = ss.str();
        }
        std::cout << cml::highlight_source(source_name, code, true);
        if (!code.empty() && code.back() != '\n') std::cout << "\n";
        return 0;
    }

    if (!has_inline) {
        return run_single_file(filename, script_args, opts, run_bc);
    }

    cml::Pipeline pipeline;
    try {
        cml::EvalResult res = pipeline.eval_source("<inline>", inline_code, opts);
        if (!res.debug_output.empty()) {
            std::cout << res.debug_output;
        }
        if (!opts.typecheck_only) {
            std::cout << cml::value_to_string(res.value) << "\n";
        }
    } catch (const cml::ProgramExit& e) {
        return e.code;
    } catch (const cml::CompileError& e) {
        cml::SourceFile sf("<inline>", inline_code);
        std::cerr << e.diagnostic().format(&sf);
        return 1;
    }
    return 0;
}
