#include "cml/pipeline.hpp"
#include "cml/lexer.hpp"
#include "cml/parser.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cml {

namespace fs = std::filesystem;

namespace {
std::string& executable_path_storage() {
    static std::string path;
    return path;
}
}  // namespace

void set_executable_path(const std::string& argv0) {
    if (argv0.empty()) return;
    std::error_code ec;
    fs::path p = fs::weakly_canonical(argv0, ec);
    executable_path_storage() = ec ? argv0 : p.string();
}

const std::string& get_executable_path() {
    return executable_path_storage();
}

Pipeline::Pipeline() {
    vm_.register_ctors(typechecker_.ctors());
    if (const char* env_std = std::getenv("CML_STDLIB")) {
        if (*env_std) search_paths_.push_back(env_std);
    }
    if (!executable_path_storage().empty()) {
        fs::path exe_dir = fs::path(executable_path_storage()).parent_path();
        if (!exe_dir.empty()) {
            search_paths_.push_back((exe_dir / ".." / "share" / "commonml" / "stdlib").string());
            search_paths_.push_back((exe_dir / "stdlib").string());
        }
    }
    search_paths_.push_back("stdlib");
#ifdef CML_STDLIB_DIR
    search_paths_.push_back(CML_STDLIB_DIR);
#endif
}

void Pipeline::add_search_path(const std::string& path) {
    if (!path.empty()) {
        search_paths_.push_back(path);
    }
}

void Pipeline::register_package_entry(const std::string& name, const std::string& entry_file) {
    package_entries_[name] = entry_file;
}

static std::optional<std::string> try_regular_file(const fs::path& p) {
    std::error_code ec;
    if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
        fs::path canon = fs::weakly_canonical(p, ec);
        return ec ? p.string() : canon.string();
    }
    return std::nullopt;
}

static std::optional<std::string> try_module_candidates(const fs::path& base,
                                                        const std::string& spec) {
    if (auto found = try_regular_file(base / spec)) return found;
    if (auto found = try_regular_file(base / (spec + ".cml"))) return found;
    if (auto found = try_regular_file(base / spec / "src" / "lib.cml")) return found;
    if (auto found = try_regular_file(base / spec / "lib.cml")) return found;
    return std::nullopt;
}

std::string Pipeline::resolve_module_path(const std::string& spec, const std::string& from_file,
                                          const SourceSpan& span) const {
    if (spec.empty()) {
        throw CompileError(Diagnostic{"import", "empty import path", span});
    }

    size_t slash = spec.find('/');
    std::string pkg_name = (slash == std::string::npos) ? spec : spec.substr(0, slash);
    auto pkg_it = package_entries_.find(pkg_name);
    if (pkg_it != package_entries_.end()) {
        if (slash == std::string::npos) {
            if (auto found = try_regular_file(pkg_it->second)) return *found;
        } else {
            fs::path pkg_dir = fs::path(pkg_it->second).parent_path();
            std::string sub = spec.substr(slash + 1);
            if (auto found = try_module_candidates(pkg_dir, sub)) return *found;
            if (auto found = try_module_candidates(pkg_dir.parent_path(), sub)) return *found;
        }
    }

    if (spec.rfind("std/", 0) == 0) {
        std::string sub = spec.substr(4);
        for (const auto& sp : search_paths_) {
            if (auto found = try_module_candidates(fs::path(sp), sub)) return *found;
        }
    }

    if (!from_file.empty() && from_file[0] != '<') {
        fs::path dir = fs::path(from_file).parent_path();
        if (!dir.empty()) {
            if (auto found = try_module_candidates(dir, spec)) return *found;
        }
    }

    if (auto found = try_module_candidates(fs::current_path(), spec)) return *found;
    for (const auto& sp : search_paths_) {
        if (auto found = try_module_candidates(fs::path(sp), spec)) return *found;
    }

    throw CompileError(Diagnostic{"import", "could not find module '" + spec + "'", span});
}

const CompiledModule& Pipeline::ensure_module_compiled(const std::string& spec,
                                                       const std::string& from_file,
                                                       const SourceSpan& span) {
    std::string resolved = resolve_module_path(spec, from_file, span);
    auto cached = compiled_modules_.find(resolved);
    if (cached != compiled_modules_.end()) {
        return cached->second;
    }
    if (loading_modules_.count(resolved)) {
        throw CompileError(Diagnostic{"import", "circular import of '" + spec + "'", span});
    }

    std::ifstream ifs(resolved);
    if (!ifs) {
        throw CompileError(Diagnostic{"import", "could not open module '" + resolved + "'", span});
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();

    loading_modules_.insert(resolved);
    try {
        SourceFile src(resolved, ss.str());
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        Program prog = parser.parse_program();
        Program expanded = expander_.expand_program(prog);

        TypeChecker mod_tc;
        mod_tc.set_module_resolver([this, resolved](const std::string& sub_spec,
                                                    const SourceSpan& sub_span) -> ModuleExports {
            return ensure_module_compiled(sub_spec, resolved, sub_span).exports;
        });
        (void)mod_tc.check_program(expanded);

        ModuleExports exports;
        exports.module_id = resolved;
        for (const auto& name : mod_tc.exported_names()) {
            const TypeScheme* sch = mod_tc.env().lookup(Ident{name, {}, {}});
            if (sch) {
                exports.values[name] = *sch;
            }
        }
        exports.structs = mod_tc.structs();
        exports.ctors = mod_tc.ctors();
        exports.type_decls = mod_tc.type_decls();

        Compiler mod_comp(&mod_tc.ctors());
        mod_comp.set_module_resolver([this, resolved](const std::string& sub_spec,
                                                      const SourceSpan& sub_span) -> std::string {
            return ensure_module_compiled(sub_spec, resolved, sub_span).global_name;
        });

        std::vector<std::string> unique_exports;
        std::unordered_set<std::string> seen_exp;
        for (const auto& nm : mod_tc.exported_names()) {
            if (seen_exp.insert(nm).second) {
                unique_exports.push_back(nm);
            }
        }
        auto proto = mod_comp.compile_module_program(expanded, unique_exports);

        CompiledModule cm;
        cm.module_id = resolved;
        cm.global_name = "$mod:" + resolved;
        cm.exports = std::move(exports);
        cm.proto = std::move(proto);
        compiled_modules_[resolved] = std::move(cm);
        module_init_order_.push_back(resolved);
    } catch (...) {
        loading_modules_.erase(resolved);
        throw;
    }
    loading_modules_.erase(resolved);
    return compiled_modules_.at(resolved);
}

void Pipeline::init_pending_modules_in_vm() {
    for (const auto& mod_id : module_init_order_) {
        if (vm_initialized_modules_.count(mod_id)) continue;
        const CompiledModule& cm = compiled_modules_.at(mod_id);
        vm_.register_ctors(cm.exports.ctors);
        Value mod_val = vm_.run(cm.proto);
        vm_.set_global(cm.global_name, std::move(mod_val));
        vm_initialized_modules_.insert(mod_id);
    }
}

EvalResult Pipeline::eval_file(const std::string& path, const EvalOptions& opts) {
    std::ifstream ifs(path);
    if (!ifs) {
        throw CompileError(Diagnostic{"io", "could not open file '" + path + "'", {}});
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(path, ec);
    std::string key = ec ? "" : canon.string();
    if (!key.empty()) {
        loading_modules_.insert(key);
    }
    try {
        EvalResult res = eval_source(path, ss.str(), opts);
        if (!key.empty()) {
            loading_modules_.erase(key);
        }
        return res;
    } catch (...) {
        if (!key.empty()) {
            loading_modules_.erase(key);
        }
        throw;
    }
}

EvalResult Pipeline::eval_source(const std::string& filename, const std::string& code,
                                 const EvalOptions& opts) {
    EvalResult res;
    std::ostringstream dbg;

    SourceFile src(filename, code);
    Lexer lexer(src);
    std::vector<Token> tokens = lexer.tokenize();

    if (opts.dump_tokens) {
        dbg << "== Tokens ==\n";
        for (const auto& t : tokens) {
            dbg << t.span.line << ":" << t.span.column << " " << token_kind_name(t.kind)
                << " '" << t.lexeme << "'\n";
        }
    }

    Parser parser(std::move(tokens));
    Program prog = parser.parse_program();

    if (opts.dump_ast) {
        dbg << "== AST ==\n";
        for (const auto& s : prog.stmts) {
            dbg << stmt_to_string(s) << "\n";
        }
    }

    Program expanded = expander_.expand_program(prog);
    if (opts.dump_expanded) {
        dbg << "== Expanded AST ==\n";
        for (const auto& s : expanded.stmts) {
            dbg << stmt_to_string(s) << "\n";
        }
    }

    typechecker_.set_module_resolver([this, filename](const std::string& spec,
                                                      const SourceSpan& span) -> ModuleExports {
        return ensure_module_compiled(spec, filename, span).exports;
    });
    res.stmt_types = typechecker_.check_program(expanded);
    if (!res.stmt_types.empty()) {
        res.type_str = res.stmt_types.back();
    }
    if (opts.dump_types) {
        dbg << "== Types ==\n";
        for (const auto& t : res.stmt_types) {
            dbg << t << "\n";
        }
    }

    if (!opts.typecheck_only) {
        init_pending_modules_in_vm();
    }
    vm_.register_ctors(typechecker_.ctors());

    Compiler compiler(&typechecker_.ctors());
    compiler.set_module_resolver([this, filename](const std::string& spec,
                                                  const SourceSpan& span) -> std::string {
        return ensure_module_compiled(spec, filename, span).global_name;
    });
    auto proto = compiler.compile_program(expanded);

    if (opts.dump_bytecode) {
        dbg << disassemble_chunk(proto->chunk, filename);
    }

    if (!opts.typecheck_only) {
        res.value = vm_.run(proto);
    }

    res.debug_output = dbg.str();
    return res;
}

EvalResult Pipeline::eval_expr(const std::string& filename, const std::string& code,
                               const EvalOptions& opts) {
    EvalResult res;
    std::ostringstream dbg;

    SourceFile src(filename, code);
    Lexer lexer(src);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(std::move(tokens));
    ExprPtr expr = parser.parse_single_expr();
    ExprPtr expanded = expander_.expand_expr(expr);

    typechecker_.set_module_resolver([this, filename](const std::string& spec,
                                                      const SourceSpan& span) -> ModuleExports {
        return ensure_module_compiled(spec, filename, span).exports;
    });
    TypePtr ty = typechecker_.infer_expr(expanded);
    res.type_str = type_to_string(ty);

    if (!opts.typecheck_only) {
        init_pending_modules_in_vm();
    }
    vm_.register_ctors(typechecker_.ctors());

    Compiler compiler(&typechecker_.ctors());
    compiler.set_module_resolver([this, filename](const std::string& spec,
                                                  const SourceSpan& span) -> std::string {
        return ensure_module_compiled(spec, filename, span).global_name;
    });
    auto proto = compiler.compile_expr(expanded);
    if (opts.dump_bytecode) {
        dbg << disassemble_chunk(proto->chunk, filename);
    }
    if (!opts.typecheck_only) {
        res.value = vm_.run(proto);
    }
    res.debug_output = dbg.str();
    return res;
}

BytecodeProgram Pipeline::compile_to_bytecode(const std::string& filename, const std::string& code,
                                              const EvalOptions& opts,
                                              std::string* debug_out) {
    std::ostringstream dbg;

    if (!filename.empty() && filename[0] != '<') {
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(filename, ec);
        if (!ec) loading_modules_.insert(canon.string());
    }

    try {
        SourceFile src(filename, code);
        Lexer lexer(src);
        std::vector<Token> tokens = lexer.tokenize();

        if (opts.dump_tokens) {
            dbg << "== Tokens ==\n";
            for (const auto& t : tokens) {
                dbg << t.span.line << ":" << t.span.column << " " << token_kind_name(t.kind)
                    << " '" << t.lexeme << "'\n";
            }
        }

        Parser parser(std::move(tokens));
        Program prog = parser.parse_program();
        if (opts.dump_ast) {
            dbg << "== AST ==\n";
            for (const auto& s : prog.stmts) {
                dbg << stmt_to_string(s) << "\n";
            }
        }

        Program expanded = expander_.expand_program(prog);
        if (opts.dump_expanded) {
            dbg << "== Expanded AST ==\n";
            for (const auto& s : expanded.stmts) {
                dbg << stmt_to_string(s) << "\n";
            }
        }

        typechecker_.set_module_resolver(
            [this, filename](const std::string& spec, const SourceSpan& span) -> ModuleExports {
                return ensure_module_compiled(spec, filename, span).exports;
            });
        std::vector<std::string> types = typechecker_.check_program(expanded);
        if (opts.dump_types) {
            dbg << "== Types ==\n";
            for (const auto& t : types) {
                dbg << t << "\n";
            }
        }

        std::vector<std::pair<std::string, std::shared_ptr<FunctionProto>>> mod_inits;
        for (const auto& mod_id : module_init_order_) {
            const CompiledModule& cm = compiled_modules_.at(mod_id);
            mod_inits.emplace_back(cm.global_name, cm.proto);
        }

        Compiler compiler(&typechecker_.ctors());
        compiler.set_module_resolver(
            [this, filename](const std::string& spec, const SourceSpan& span) -> std::string {
                return ensure_module_compiled(spec, filename, span).global_name;
            });
        auto entry = compiler.compile_program_with_modules(expanded, mod_inits);

        if (opts.dump_bytecode) {
            dbg << disassemble_chunk(entry->chunk, filename);
        }
        if (debug_out) {
            *debug_out = dbg.str();
        }

        BytecodeProgram bp;
        for (const auto& [cname, info] : typechecker_.ctors()) {
            bp.ctors.push_back(BytecodeCtor{cname, info.tag, info.arity});
        }
        bp.entry = std::move(entry);
        return bp;
    } catch (...) {
        if (!filename.empty() && filename[0] != '<') {
            std::error_code ec;
            fs::path canon = fs::weakly_canonical(filename, ec);
            if (!ec) loading_modules_.erase(canon.string());
        }
        throw;
    }
}

}  // namespace cml
