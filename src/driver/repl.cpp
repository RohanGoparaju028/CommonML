#include "cml/repl.hpp"

#include <sstream>
#include <string>

namespace cml {

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool Repl::handle_command(const std::string& line, std::ostream& out, std::ostream& err,
                          bool& should_quit) {
    if (line == ":quit" || line == ":q") {
        should_quit = true;
        return true;
    }
    if (line == ":help" || line == ":h") {
        out << "Commands:\n"
            << "  :help, :h        Show this help\n"
            << "  :quit, :q        Exit the REPL\n"
            << "  :type <expr>     Show inferred type without executing\n"
            << "  :ast             Toggle AST dump\n"
            << "  :bc              Toggle bytecode dump\n"
            << "  :reset           Reset REPL state\n";
        return true;
    }
    if (line == ":ast") {
        opts_.dump_ast = !opts_.dump_ast;
        out << "AST dump: " << (opts_.dump_ast ? "on" : "off") << "\n";
        return true;
    }
    if (line == ":bc") {
        opts_.dump_bytecode = !opts_.dump_bytecode;
        out << "Bytecode dump: " << (opts_.dump_bytecode ? "on" : "off") << "\n";
        return true;
    }
    if (line == ":reset") {
        pipeline_ = Pipeline();
        eval_counter_ = 1;
        out << "State reset.\n";
        return true;
    }
    if (line.rfind(":type ", 0) == 0) {
        std::string expr_src = trim(line.substr(6));
        try {
            EvalResult res = pipeline_.eval_expr("<repl>", expr_src, EvalOptions{});
            out << ": " << res.type_str << "\n";
        } catch (const CompileError& e) {
            SourceFile sf("<repl>", expr_src);
            err << e.diagnostic().format(&sf);
        }
        return true;
    }
    err << "Unknown command: " << line << "\n";
    return true;
}

int Repl::run(std::istream& in, std::ostream& out, std::ostream& err) {
    out << "CommonML REPL (type :help for commands, :quit to exit)\n";
    std::string buffer;
    while (true) {
        out << (buffer.empty() ? "cml> " : "...> ") << std::flush;
        std::string line;
        if (!std::getline(in, line)) {
            out << "\n";
            break;
        }
        std::string trimmed = trim(line);
        if (buffer.empty() && trimmed.empty()) {
            continue;
        }
        if (buffer.empty() && trimmed[0] == ':') {
            bool quit = false;
            handle_command(trimmed, out, err, quit);
            if (quit) break;
            continue;
        }
        if (!buffer.empty()) buffer += "\n";
        buffer += line;

        // If line ends with '\', keep reading multi-line input.
        if (!trimmed.empty() && trimmed.back() == '\\') {
            buffer.pop_back();
            continue;
        }

        std::string filename = "<repl:" + std::to_string(eval_counter_++) + ">";
        try {
            EvalResult res = pipeline_.eval_source(filename, buffer, opts_);
            if (!res.debug_output.empty()) {
                out << res.debug_output;
            }
            if (!res.stmt_types.empty()) {
                out << value_to_string(res.value) << " : " << res.type_str << "\n";
            }
        } catch (const CompileError& e) {
            SourceFile sf(filename, buffer);
            err << e.diagnostic().format(&sf);
        }
        buffer.clear();
    }
    return 0;
}

}  // namespace cml
