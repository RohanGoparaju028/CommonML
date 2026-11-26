#pragma once

#include "cml/ast.hpp"
#include "cml/bytecode.hpp"
#include "cml/compiler.hpp"
#include "cml/macro_expander.hpp"
#include "cml/source.hpp"
#include "cml/typecheck.hpp"
#include "cml/vm.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cml {

void set_executable_path(const std::string& argv0);
const std::string& get_executable_path();

struct EvalOptions {
    bool dump_tokens = false;
    bool dump_ast = false;
    bool dump_expanded = false;
    bool dump_types = false;
    bool dump_bytecode = false;
    bool typecheck_only = false;
};

struct EvalResult {
    Value value = Value::unit();
    std::string type_str;
    std::vector<std::string> stmt_types;
    std::string debug_output;
};

struct CompiledModule {
    std::string module_id;
    std::string global_name;
    ModuleExports exports;
    std::shared_ptr<FunctionProto> proto;
};

class Pipeline {
public:
    Pipeline();

    void add_search_path(const std::string& path);
    void register_package_entry(const std::string& name, const std::string& entry_file);

    EvalResult eval_source(const std::string& filename, const std::string& code,
                           const EvalOptions& opts = {});
    EvalResult eval_file(const std::string& path, const EvalOptions& opts = {});
    EvalResult eval_expr(const std::string& filename, const std::string& code,
                         const EvalOptions& opts = {});
    BytecodeProgram compile_to_bytecode(const std::string& filename, const std::string& code,
                                        const EvalOptions& opts = {},
                                        std::string* debug_out = nullptr);

    const TypeChecker& typechecker() const { return typechecker_; }
    TypeChecker& typechecker_mut() { return typechecker_; }
    const VM& vm() const { return vm_; }
    VM& vm_mut() { return vm_; }

private:
    std::string resolve_module_path(const std::string& spec, const std::string& from_file,
                                    const SourceSpan& span) const;
    const CompiledModule& ensure_module_compiled(const std::string& spec,
                                                 const std::string& from_file,
                                                 const SourceSpan& span);
    void init_pending_modules_in_vm();

    MacroExpander expander_;
    TypeChecker typechecker_;
    VM vm_;
    std::vector<std::string> search_paths_;
    std::unordered_map<std::string, std::string> package_entries_;
    std::unordered_map<std::string, CompiledModule> compiled_modules_;
    std::vector<std::string> module_init_order_;
    std::unordered_set<std::string> vm_initialized_modules_;
    std::unordered_set<std::string> loading_modules_;
};

}  // namespace cml
