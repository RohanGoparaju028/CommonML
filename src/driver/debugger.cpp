#include "cml/debugger.hpp"
#include "cml/builtins.hpp"
#include "cml/package.hpp"
#include "cml/pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace cml {

namespace fs = std::filesystem;

namespace {

std::string read_file_text(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string json_extract_string(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";
    ++pos;
    std::string out;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(json[pos]); break;
            }
        } else {
            out.push_back(json[pos]);
        }
        ++pos;
    }
    return out;
}

long long json_extract_int(const std::string& json, const std::string& key, long long def = 0) {
    std::string pat = "\"" + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return def;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    bool neg = false;
    if (pos < json.size() && json[pos] == '-') {
        neg = true;
        ++pos;
    }
    if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos]))) return def;
    long long val = 0;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        val = val * 10 + (json[pos] - '0');
        ++pos;
    }
    return neg ? -val : val;
}

std::vector<long long> json_extract_lines_array(const std::string& json) {
    std::vector<long long> out;
    size_t pos = json.find("\"breakpoints\"");
    if (pos == std::string::npos) return out;
    size_t arr_end = json.find(']', pos);
    if (arr_end == std::string::npos) return out;
    std::string sub = json.substr(pos, arr_end - pos);
    size_t cur = 0;
    while ((cur = sub.find("\"line\"", cur)) != std::string::npos) {
        long long ln = json_extract_int(sub.substr(cur), "line", -1);
        if (ln > 0) out.push_back(ln);
        cur += 6;
    }
    return out;
}

struct DebugVar {
    std::string name;
    Value value;
};

std::vector<DebugVar> collect_frame_locals(const VM& vm, size_t frame_idx) {
    std::vector<DebugVar> vars;
    if (frame_idx >= vm.frames().size()) return vars;
    const auto& fr = vm.frames()[frame_idx];
    const auto& lnames = fr.closure.proto->chunk.local_names;
    size_t nlocs = fr.closure.proto->num_locals;
    for (size_t i = 0; i < nlocs; ++i) {
        size_t slot = fr.base_slot + i;
        if (slot >= vm.stack().size()) break;
        std::string nm = (i < lnames.size() && !lnames[i].empty())
                             ? lnames[i]
                             : ("$" + std::to_string(i));
        if (!nm.empty() && nm[0] == '$') continue;
        vars.push_back(DebugVar{nm, vm.stack()[slot]});
    }
    return vars;
}

const Value* lookup_debug_var(const VM& vm, const std::string& name, std::string& err_out) {
    if (!vm.frames().empty()) {
        size_t fidx = vm.frames().size() - 1;
        const auto& fr = vm.frames()[fidx];
        const auto& lnames = fr.closure.proto->chunk.local_names;
        for (size_t i = lnames.size(); i-- > 0;) {
            if (lnames[i] == name) {
                size_t slot = fr.base_slot + i;
                if (slot < vm.stack().size()) return &vm.stack()[slot];
            }
        }
    }
    if (const Value* g = vm.get_global(name)) {
        return g;
    }
    err_out = "unknown variable '" + name + "'";
    return nullptr;
}

std::string eval_debug_lookup(const VM& vm, const std::string& expr_raw) {
    std::string s = trim(expr_raw);
    if (s.empty()) return "usage: p <var|var->field|var.field|*ptr>";
    bool deref = false;
    if (s[0] == '*') {
        deref = true;
        s = trim(s.substr(1));
    }
    size_t arrow = s.find("->");
    size_t dot = s.find('.');
    std::string base = s;
    std::string field;
    bool via_ptr = false;
    if (arrow != std::string::npos && (dot == std::string::npos || arrow < dot)) {
        base = trim(s.substr(0, arrow));
        field = trim(s.substr(arrow + 2));
        via_ptr = true;
    } else if (dot != std::string::npos) {
        base = trim(s.substr(0, dot));
        field = trim(s.substr(dot + 1));
    }

    std::string err;
    const Value* val = lookup_debug_var(vm, base, err);
    if (!val) return err;

    Value cur = *val;
    if (deref || via_ptr) {
        auto* pv = std::get_if<ValPtr>(&cur.node);
        if (!pv || !pv->ptr) return "'" + base + "' is not a pointer";
        if (!pv->ptr->alive) return "'" + base + "' is closed or dangling";
        if (pv->ptr->is_stack_slot) {
            if (pv->ptr->stack_slot >= vm.stack().size()) return "'" + base + "' is out of scope";
            cur = vm.stack()[pv->ptr->stack_slot];
        } else if (pv->ptr->is_global) {
            const Value* g = vm.get_global(pv->ptr->global_name);
            if (!g) return "'" + base + "' global missing";
            cur = *g;
        } else {
            cur = pv->ptr->value;
        }
    }
    if (!field.empty()) {
        auto* st = std::get_if<ValStruct>(&cur.node);
        if (!st) return "cannot inspect field '" + field + "' on non-struct value";
        auto it = st->ptr->fields.find(field);
        if (it == st->ptr->fields.end()) return "no field '" + field + "'";
        cur = it->second;
    }
    return value_to_string(cur);
}

bool prepare_vm(const DebugSessionConfig& cfg, VM& vm, std::shared_ptr<FunctionProto>& entry,
                std::string& source_text, std::ostream& err) {
    source_text = read_file_text(cfg.entry_file);
    if (source_text.empty() && !fs::exists(cfg.entry_file)) {
        err << "debug error: could not open '" << cfg.entry_file << "'\n";
        return false;
    }
    set_cli_args(cfg.args);
    Pipeline pipeline;
    std::string manifest = PackageManager::find_manifest(cfg.entry_file);
    if (!manifest.empty()) {
        try {
            PackageManager::configure_pipeline(pipeline, fs::path(manifest).parent_path().string());
        } catch (const CompileError&) {
        }
    }
    try {
        BytecodeProgram bp = pipeline.compile_to_bytecode(cfg.entry_file, source_text);
        vm.register_ctors(pipeline.typechecker().ctors());
        for (const auto& c : bp.ctors) {
            if (c.arity == 0) {
                vm.set_global(c.name, Value::make_adt(c.tag, c.name, {}));
            } else {
                vm.set_global(c.name, Value{ValCtor{c.tag, c.name, c.arity, {}}});
            }
        }
        entry = bp.entry;
        return true;
    } catch (const CompileError& e) {
        SourceFile sf(cfg.entry_file, source_text);
        err << e.diagnostic().format(&sf);
        return false;
    }
}

}  // namespace

int Debugger::run_cli(const DebugSessionConfig& cfg, std::istream& in, std::ostream& out,
                      std::ostream& err) {
    VM vm;
    std::shared_ptr<FunctionProto> entry;
    std::string source_text;
    if (!prepare_vm(cfg, vm, entry, source_text, err)) {
        return 1;
    }

    auto src_lines = split_lines(source_text);
    std::set<size_t> breakpoints(cfg.initial_breakpoints.begin(), cfg.initial_breakpoints.end());
    enum class StepMode { Continue, StepIn, StepOver, StepOut };
    StepMode mode = cfg.stop_on_entry ? StepMode::StepIn : StepMode::Continue;
    size_t last_line = 0;
    size_t step_depth = 1;
    bool quit_requested = false;

    out << "CommonML Debugger attached to " << cfg.entry_file << "\n"
        << "Commands: b <line>, d <line>, c, n, s, o, p <expr>, locals, heap, bt, l, q\n";

    vm.set_debug_hook([&](const VM& v, const Instruction& ins) {
        if (quit_requested) {
            throw ProgramExit{0};
        }
        if (ins.span.line == 0) return;

        size_t cur_depth = v.frames().size();
        size_t cur_line = ins.span.line;
        bool hit_bp = breakpoints.count(cur_line) > 0 && cur_line != last_line;
        bool should_stop = hit_bp;
        if (!should_stop) {
            if (mode == StepMode::StepIn && cur_line != last_line) {
                should_stop = true;
            } else if (mode == StepMode::StepOver && cur_depth <= step_depth &&
                       cur_line != last_line) {
                should_stop = true;
            } else if (mode == StepMode::StepOut && cur_depth < step_depth &&
                       cur_line != last_line) {
                should_stop = true;
            }
        }
        if (!should_stop) return;

        last_line = cur_line;
        std::string line_preview =
            (cur_line >= 1 && cur_line <= src_lines.size()) ? src_lines[cur_line - 1] : "";
        std::string fn_name =
            v.frames().empty() ? "<top>" : v.frames().back().closure.proto->name;
        out << (hit_bp ? "[breakpoint] " : "[stop] ") << fn_name << " at line " << cur_line
            << (line_preview.empty() ? "" : ": " + trim(line_preview)) << "\n";

        while (true) {
            out << "(cml-dbg) " << std::flush;
            std::string cmd_line;
            if (!std::getline(in, cmd_line)) {
                mode = StepMode::Continue;
                return;
            }
            std::string cmd = trim(cmd_line);
            if (cmd.empty()) continue;

            std::istringstream cs(cmd);
            std::string op;
            cs >> op;
            if (op == "c" || op == "continue") {
                mode = StepMode::Continue;
                return;
            } else if (op == "s" || op == "step") {
                mode = StepMode::StepIn;
                step_depth = cur_depth;
                return;
            } else if (op == "n" || op == "next") {
                mode = StepMode::StepOver;
                step_depth = cur_depth;
                return;
            } else if (op == "o" || op == "out") {
                mode = StepMode::StepOut;
                step_depth = cur_depth;
                return;
            } else if (op == "b" || op == "break") {
                size_t ln = 0;
                if (cs >> ln && ln > 0) {
                    breakpoints.insert(ln);
                    out << "Breakpoint set at line " << ln << "\n";
                } else {
                    out << "Usage: b <line>\n";
                }
            } else if (op == "d" || op == "delete") {
                size_t ln = 0;
                if (cs >> ln) {
                    breakpoints.erase(ln);
                    out << "Deleted breakpoint at line " << ln << "\n";
                }
            } else if (op == "locals") {
                if (v.frames().empty()) {
                    out << "(no active frame)\n";
                } else {
                    auto vars = collect_frame_locals(v, v.frames().size() - 1);
                    if (vars.empty()) out << "(no locals)\n";
                    for (const auto& dv : vars) {
                        out << "  " << dv.name << " = " << value_to_string(dv.value) << "\n";
                    }
                }
            } else if (op == "p" || op == "print") {
                std::string rest;
                std::getline(cs, rest);
                out << eval_debug_lookup(v, rest) << "\n";
            } else if (op == "heap") {
                out << "live heap allocations: "
                    << live_heap_allocations().load(std::memory_order_relaxed) << "\n";
            } else if (op == "bt") {
                for (size_t i = v.frames().size(); i-- > 0;) {
                    const auto& fr = v.frames()[i];
                    size_t ln = 0;
                    if (fr.ip > 0 && fr.ip - 1 < fr.closure.proto->chunk.code.size()) {
                        ln = fr.closure.proto->chunk.code[fr.ip - 1].span.line;
                    }
                    out << "  #" << (v.frames().size() - 1 - i) << " "
                        << fr.closure.proto->name << " (line " << ln << ")\n";
                }
            } else if (op == "l" || op == "list") {
                size_t lo = cur_line > 3 ? cur_line - 3 : 1;
                size_t hi = std::min(src_lines.size(), cur_line + 3);
                for (size_t ln = lo; ln <= hi; ++ln) {
                    out << (ln == cur_line ? "=> " : "   ") << ln << ": " << src_lines[ln - 1]
                        << "\n";
                }
            } else if (op == "q" || op == "quit") {
                quit_requested = true;
                throw ProgramExit{0};
            } else {
                out << "Unknown command '" << op
                    << "'. Try: b, d, c, n, s, o, p, locals, heap, bt, l, q\n";
            }
        }
    });

    try {
        Value result = vm.run(entry);
        if (!std::holds_alternative<ValUnit>(result.node)) {
            out << "Program exited with value: " << value_to_string(result) << "\n";
        } else {
            out << "Program exited.\n";
        }
        return 0;
    } catch (const ProgramExit& e) {
        return e.code;
    } catch (const CompileError& e) {
        SourceFile sf(cfg.entry_file, source_text);
        err << e.diagnostic().format(&sf);
        return 1;
    }
}

int Debugger::run_dap(std::istream& in, std::ostream& out, std::ostream& err) {
    auto send_msg = [&](const std::string& body) {
        out << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
    };

    int seq_counter = 1;
    auto send_resp = [&](long long req_seq, const std::string& cmd, bool ok,
                         const std::string& body = "{}", const std::string& msg = "") {
        std::ostringstream os;
        os << "{\"seq\":" << seq_counter++ << ",\"type\":\"response\",\"request_seq\":"
           << req_seq << ",\"success\":" << (ok ? "true" : "false") << ",\"command\":\""
           << json_escape(cmd) << "\",\"body\":" << body;
        if (!ok && !msg.empty()) os << ",\"message\":\"" << json_escape(msg) << "\"";
        os << "}";
        send_msg(os.str());
    };
    auto send_event = [&](const std::string& evt, const std::string& body = "{}") {
        std::ostringstream os;
        os << "{\"seq\":" << seq_counter++ << ",\"type\":\"event\",\"event\":\""
           << json_escape(evt) << "\",\"body\":" << body << "}";
        send_msg(os.str());
    };

    auto read_msg = [&](std::string& payload) -> bool {
        std::string line;
        size_t content_len = 0;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            if (line.rfind("Content-Length:", 0) == 0) {
                content_len = static_cast<size_t>(std::stoull(trim(line.substr(15))));
            }
        }
        if (content_len == 0) return false;
        payload.resize(content_len);
        in.read(payload.data(), static_cast<std::streamsize>(content_len));
        return static_cast<size_t>(in.gcount()) == content_len;
    };

    std::mutex mtx;
    std::condition_variable cv;
    std::set<size_t> breakpoints;
    enum class Action { None, Continue, StepIn, StepOver, StepOut, Quit };
    Action next_action = Action::None;
    bool launched = false;
    bool vm_done = false;
    const VM* paused_vm = nullptr;
    std::string program_path;
    std::thread vm_thread;

    std::string raw;
    while (read_msg(raw)) {
        long long seq = json_extract_int(raw, "seq", 0);
        std::string cmd = json_extract_string(raw, "command");
        if (cmd == "initialize") {
            send_resp(seq, cmd, true,
                      "{\"supportsConfigurationDoneRequest\":true,\"supportsEvaluateForHovers\":true}");
            send_event("initialized");
        } else if (cmd == "setBreakpoints") {
            auto lines = json_extract_lines_array(raw);
            {
                std::lock_guard<std::mutex> lk(mtx);
                breakpoints.clear();
                for (long long ln : lines) breakpoints.insert(static_cast<size_t>(ln));
            }
            std::ostringstream bps;
            bps << "{\"breakpoints\":[";
            for (size_t i = 0; i < lines.size(); ++i) {
                if (i > 0) bps << ",";
                bps << "{\"verified\":true,\"line\":" << lines[i] << "}";
            }
            bps << "]}";
            send_resp(seq, cmd, true, bps.str());
        } else if (cmd == "launch") {
            program_path = json_extract_string(raw, "program");
            send_resp(seq, cmd, true);
            if (!launched && !program_path.empty()) {
                launched = true;
                vm_thread = std::thread([&]() {
                    DebugSessionConfig cfg;
                    cfg.entry_file = program_path;
                    cfg.stop_on_entry = true;
                    VM vm;
                    std::shared_ptr<FunctionProto> entry;
                    std::string src;
                    if (!prepare_vm(cfg, vm, entry, src, err)) {
                        send_event("terminated");
                        return;
                    }
                    Action cur_mode = Action::StepIn;
                    size_t step_depth = 1;
                    size_t last_line = 0;
                    vm.set_debug_hook([&](const VM& v, const Instruction& ins) {
                        if (ins.span.line == 0) return;
                        std::unique_lock<std::mutex> lk(mtx);
                        bool hit_bp =
                            breakpoints.count(ins.span.line) > 0 && ins.span.line != last_line;
                        size_t d = v.frames().size();
                        bool stop = hit_bp ||
                                    (cur_mode == Action::StepIn && ins.span.line != last_line) ||
                                    (cur_mode == Action::StepOver && d <= step_depth &&
                                     ins.span.line != last_line) ||
                                    (cur_mode == Action::StepOut && d < step_depth &&
                                     ins.span.line != last_line);
                        if (!stop) return;
                        last_line = ins.span.line;
                        paused_vm = &v;
                        next_action = Action::None;
                        send_event("stopped", std::string("{\"reason\":\"") +
                                                  (hit_bp ? "breakpoint" : "step") +
                                                  "\",\"threadId\":1}");
                        cv.wait(lk, [&]() { return next_action != Action::None; });
                        cur_mode = next_action;
                        step_depth = d;
                        paused_vm = nullptr;
                        if (cur_mode == Action::Quit) throw ProgramExit{0};
                    });
                    try {
                        vm.run(entry);
                    } catch (...) {
                    }
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        vm_done = true;
                    }
                    send_event("terminated");
                });
            }
        } else if (cmd == "configurationDone") {
            send_resp(seq, cmd, true);
        } else if (cmd == "threads") {
            send_resp(seq, cmd, true, "{\"threads\":[{\"id\":1,\"name\":\"CommonML VM\"}]}");
        } else if (cmd == "stackTrace") {
            std::lock_guard<std::mutex> lk(mtx);
            std::ostringstream st;
            st << "{\"stackFrames\":[";
            if (paused_vm) {
                const auto& frs = paused_vm->frames();
                for (size_t i = frs.size(); i-- > 0;) {
                    if (i + 1 < frs.size()) st << ",";
                    const auto& fr = frs[i];
                    size_t ln = 1;
                    if (fr.ip > 0 && fr.ip - 1 < fr.closure.proto->chunk.code.size()) {
                        size_t cln = fr.closure.proto->chunk.code[fr.ip - 1].span.line;
                        if (cln > 0) ln = cln;
                    }
                    st << "{\"id\":" << (i + 1) << ",\"name\":\""
                       << json_escape(fr.closure.proto->name) << "\",\"source\":{\"path\":\""
                       << json_escape(program_path) << "\"},\"line\":" << ln << ",\"column\":1}";
                }
            }
            st << "]}";
            send_resp(seq, cmd, true, st.str());
        } else if (cmd == "scopes") {
            long long fid = json_extract_int(raw, "frameId", 1);
            send_resp(seq, cmd, true,
                      "{\"scopes\":[{\"name\":\"Locals\",\"variablesReference\":" +
                          std::to_string(fid * 1000 + 1) + ",\"expensive\":false}]}");
        } else if (cmd == "variables") {
            long long vref = json_extract_int(raw, "variablesReference", 1001);
            size_t fidx = vref >= 1000 ? static_cast<size_t>(vref / 1000 - 1) : 0;
            std::lock_guard<std::mutex> lk(mtx);
            std::ostringstream vs;
            vs << "{\"variables\":[";
            if (paused_vm && fidx < paused_vm->frames().size()) {
                auto vars = collect_frame_locals(*paused_vm, fidx);
                for (size_t i = 0; i < vars.size(); ++i) {
                    if (i > 0) vs << ",";
                    vs << "{\"name\":\"" << json_escape(vars[i].name) << "\",\"value\":\""
                       << json_escape(value_to_string(vars[i].value))
                       << "\",\"variablesReference\":0}";
                }
            }
            vs << "]}";
            send_resp(seq, cmd, true, vs.str());
        } else if (cmd == "evaluate") {
            std::string expr = json_extract_string(raw, "expression");
            std::lock_guard<std::mutex> lk(mtx);
            std::string res = paused_vm ? eval_debug_lookup(*paused_vm, expr) : "<not paused>";
            send_resp(seq, cmd, true,
                      "{\"result\":\"" + json_escape(res) + "\",\"variablesReference\":0}");
        } else if (cmd == "continue" || cmd == "next" || cmd == "stepIn" || cmd == "stepOut") {
            {
                std::lock_guard<std::mutex> lk(mtx);
                if (cmd == "continue") next_action = Action::Continue;
                else if (cmd == "next") next_action = Action::StepOver;
                else if (cmd == "stepIn") next_action = Action::StepIn;
                else next_action = Action::StepOut;
            }
            cv.notify_all();
            send_resp(seq, cmd, true, "{\"allThreadsContinued\":true}");
        } else if (cmd == "disconnect") {
            {
                std::lock_guard<std::mutex> lk(mtx);
                next_action = Action::Quit;
            }
            cv.notify_all();
            send_resp(seq, cmd, true);
            break;
        } else {
            send_resp(seq, cmd, true);
        }
    }
    if (vm_thread.joinable()) vm_thread.join();
    (void)vm_done;
    return 0;
}

}  // namespace cml
