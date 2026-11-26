#include "test_framework.hpp"
#include "cml/builtins.hpp"
#include "cml/debugger.hpp"
#include "cml/highlight.hpp"
#include "cml/package.hpp"
#include "cml/pipeline.hpp"
#include "cml/toolchain.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace cml;
namespace fs = std::filesystem;

namespace {

struct TempDirGuard {
    fs::path path;
    explicit TempDirGuard(const std::string& prefix) {
        auto ms = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / (prefix + "_" + std::to_string(ms));
        fs::create_directories(path);
    }
    ~TempDirGuard() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

CML_TEST(platform_math_builtins_and_stdlib) {
    Pipeline p;
    EvalResult res = p.eval_source("math_test.cml", R"(
let math = import "std/math"

let s = math.sqrt 25.0 +. math.pow 2.0 3.0 +. math.floor 3.9 +. math.ceil 2.1
let g = math.gcd 48 18
let l = math.lcm 12 18
let f = math.factorial 5
let pr = math.is_prime 29
let m = math.mean_float [2.0, 4.0, 6.0, 8.0]
let n = math.vec_norm [3.0, 4.0]
(int_of_float s, g, l, f, pr, int_of_float m, int_of_float n)
)");
    CML_ASSERT_EQ(value_to_string(res.value), std::string("(19, 6, 36, 120, true, 5, 5)"));
}

CML_TEST(platform_pure_commonml_trig_and_exp_log) {
    Pipeline p;
    EvalResult res = p.eval_source("trig_test.cml", R"(
let math = import "std/math"

let ok_sin = math.fabs (math.sin (math.pi /. 6.0) -. 0.5) < 1.0e-12
let ok_cos = math.fabs (math.cos 0.0 -. 1.0) < 1.0e-12
let ok_exp = math.fabs (math.exp 1.0 -. math.e) < 1.0e-12
let ok_log = math.fabs (math.log (math.exp 3.5) -. 3.5) < 1.0e-12
let ok_atan = math.fabs (math.atan 1.0 -. math.pi /. 4.0) < 1.0e-12
let ok_pow = math.fabs (math.pow 9.0 0.5 -. 3.0) < 1.0e-12
(ok_sin, ok_cos, ok_exp, ok_log, ok_atan, ok_pow)
)");
    CML_ASSERT_EQ(value_to_string(res.value),
                  std::string("(true, true, true, true, true, true)"));
}

CML_TEST(platform_import_dedup_and_cycle_detection) {
    TempDirGuard tmp("cml_mod_test");
    {
        std::ofstream a(tmp.path / "a.cml");
        a << "let base_val = 40\n";
        std::ofstream b(tmp.path / "b.cml");
        b << "let a = import \"a.cml\"\nlet b_val = a.base_val + 2\n";
        std::ofstream c(tmp.path / "c.cml");
        c << "let a = import \"a.cml\"\nlet b = import \"b.cml\"\nb.b_val\n";
    }
    Pipeline p;
    EvalResult res = p.eval_file((tmp.path / "c.cml").string());
    CML_ASSERT_EQ(value_to_string(res.value), std::string("42"));

    {
        std::ofstream cyc1(tmp.path / "cyc1.cml");
        cyc1 << "let c2 = import \"cyc2.cml\"\nlet x = 1\n";
        std::ofstream cyc2(tmp.path / "cyc2.cml");
        cyc2 << "let c1 = import \"cyc1.cml\"\nlet y = 2\n";
    }
    Pipeline p2;
    CML_ASSERT_THROWS(p2.eval_file((tmp.path / "cyc1.cml").string()), CompileError);
}

CML_TEST(platform_package_manager_templates_and_deps) {
    TempDirGuard tmp("cml_pkg_test");
    fs::path lib_dir = tmp.path / "geom_lib";
    fs::path cli_dir = tmp.path / "geom_cli";
    fs::path web_dir = tmp.path / "my_web";

    PackageManager::create_project(lib_dir.string(), "lib", "geom_lib");
    PackageManager::create_project(cli_dir.string(), "console", "geom_cli");
    PackageManager::create_project(web_dir.string(), "web", "my_web");

    // Add dependency from geom_cli -> geom_lib and use it via assignment import
    PackageManager::add_dependency(cli_dir.string(), "geom_lib", "../geom_lib");
    {
        std::ofstream main_cml(cli_dir / "src" / "main.cml", std::ios::trunc);
        main_cml << "let geom = import \"geom_lib\"\n"
                 << "let st = geom.summarize [10.0, 20.0, 30.0]\n"
                 << "let args = cli_args ()\n"
                 << "let _ = if st.count != 3 || int_of_float st.total != 60 || int_of_float st.avg != 20 then panic \"bad stats\" else ()\n"
                 << "let _ = if list_len args != 2 then panic \"expected 2 cli args\" else ()\n"
                 << "0\n";
    }

    std::ostringstream out;
    std::ostringstream err;
    CML_ASSERT_EQ(PackageManager::cmd_install(cli_dir.string(), out), 0);
    CML_ASSERT(fs::exists(cli_dir / "cml.lock"));

    CML_ASSERT_EQ(PackageManager::cmd_build(lib_dir.string(), out), 0);
    CML_ASSERT_EQ(PackageManager::cmd_build(cli_dir.string(), out), 0);
    CML_ASSERT_EQ(PackageManager::cmd_build(web_dir.string(), out), 0);

    CML_ASSERT_EQ(PackageManager::cmd_test(lib_dir.string(), out, err), 0);
    CML_ASSERT_EQ(PackageManager::cmd_test(cli_dir.string(), out, err), 0);
    CML_ASSERT_EQ(PackageManager::cmd_test(web_dir.string(), out, err), 0);

    CML_ASSERT_EQ(PackageManager::cmd_run(cli_dir.string(), {"alpha", "beta"}, out, err), 0);
    CML_ASSERT_EQ(PackageManager::cmd_list(cli_dir.string(), out), 0);
}

CML_TEST(platform_socket_and_http_roundtrip) {
    Pipeline p;
    EvalResult res = p.eval_source("http_roundtrip.cml", R"(
let http = import "std/http"

let handler req =
  match req with
  | Req GET "/hello" _ _ -> http.ok_json "{\"ok\":true}"
  | _ -> http.not_found "nope"

let srv_fd = tcp_listen "127.0.0.1" 0
let port = tcp_local_port srv_fd
let cli_fd = tcp_connect "127.0.0.1" port
let acc_fd = tcp_accept srv_fd
let _ = sock_send cli_fd "GET /hello HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
let _ = http.http_handle_client handler acc_fd
let _ = sock_close acc_fd
let _ = sock_close srv_fd
let resp = sock_recv_all cli_fd 65536
let _ = sock_close cli_fd
resp
)");
    std::string resp = std::get<ValString>(res.value.node).value;
    CML_ASSERT(resp.find("HTTP/1.1 200 OK") != std::string::npos);
    CML_ASSERT(resp.find("{\"ok\":true}") != std::string::npos);
}

CML_TEST(platform_bootstrap_commonml_language) {
    fs::path boot_dir = fs::path(CML_STDLIB_DIR).parent_path() / "bootstrap";
    fs::path boot_main = boot_dir / "main.cml";
    Pipeline p;
    set_cli_args({"-e", R"(
type Option a = None | Some a
let unwrap d o = match o with | None -> d | Some v -> v
let rec fib n = if n <= 1 then n else fib (n - 1) + fib (n - 2)
let xs = [1, 2, 3, 4]
let sum_sq =
  let rec go ys = match ys with | [] -> 0 | h :: t -> h * h + go t in
  go xs
(fib 8, sum_sq, unwrap 0 (Some 99), int_of_float (float_of_int 12))
)"});
    EvalResult res = p.eval_file(boot_main.string());
    (void)res;

    // Compile via bootstrap compiler -> serialize to CMLBC bytecode -> run natively on VM
    set_cli_args({});
    Pipeline p2;
    p2.add_search_path(boot_dir.string());
    EvalResult r2 = p2.eval_source("boot_check.cml", R"(
let lex = import "lexer.cml"
let prs = import "parser.cml"
let tc = import "typecheck.cml"
let bc = import "bytecode.cml"
let comp = import "compiler.cml"
let stmts = prs.parse_program_tokens (lex.lex_source "let x = [10, 20, 30]\nmatch x with | h :: _ -> h * 2 | [] -> 0")
let _ = tc.check_program_stmts stmts
let bc_str = bc.serialize_bprogram (comp.compile_program_to_bc stmts)
vm_exec_bytecode bc_str
)");
    CML_ASSERT_EQ(value_to_string(r2.value), std::string("20"));

    // Verify compiled bytecode also runs directly via deserialize_bytecode + VM
    EvalResult r3 = p2.eval_source("boot_bc.cml", R"(
let stmts = prs.parse_program_tokens (lex.lex_source "let rec fib n = if n <= 1 then n else fib (n - 1) + fib (n - 2)\nfib 10")
let _ = tc.check_program_stmts stmts
bc.serialize_bprogram (comp.compile_program_to_bc stmts)
)");
    BytecodeProgram bp = deserialize_bytecode(std::get<ValString>(r3.value.node).value);
    VM native_vm;
    CML_ASSERT_EQ(value_to_string(native_vm.run(bp.entry)), std::string("55"));

    // Verify bootstrap typechecker rejects type errors
    CML_ASSERT_THROWS(p2.eval_source("boot_err.cml", R"(
tc.check_program_stmts (prs.parse_program_tokens (lex.lex_source "1 + true"))
)"),
                      CompileError);

    // Verify self-hosted bootstrap compiler compiles new systems syntax:
    // `let rect = struct:`, `new rect { .x = 10, .y = 20 }`, `fn() : i32:`, `->`, `defer close`, `String`/`Bool`
    EvalResult r4 = p2.eval_source("boot_sys.cml", R"(
let prog = "
let rect = struct:
  x: i32
  y: i32
  label: str
  active: bool

let area = fn() : i32:
  let var_rect = new rect { .x = 10, .y = 20, .label = \"box\", .active = true }
  defer close var_rect
  if var_rect->active && str_len var_rect->label == 3 =
    var_rect->x * var_rect->y
  else =
    0

let res = area ()
(res, heap_live_count ())
"
let stmts = prs.parse_program_tokens (lex.lex_source prog)
let _ = tc.check_program_stmts stmts
let bc_str = bc.serialize_bprogram (comp.compile_program_to_bc stmts)
vm_exec_bytecode bc_str
)");
    CML_ASSERT_EQ(value_to_string(r4.value), std::string("(200, 0)"));
}

CML_TEST(platform_syntax_highlighting) {
    std::string src = "-- comment\nlet x = Some (42, \"hi\")\n";
    std::string plain = highlight_source("hl.cml", src, false);
    CML_ASSERT_EQ(plain, src);

    std::string ansi = highlight_source("hl.cml", src, true);
    CML_ASSERT(ansi.find("\033[2;37m--") != std::string::npos);
    CML_ASSERT(ansi.find("\033[1;35mlet\033[0m") != std::string::npos);
    CML_ASSERT(ansi.find("\033[1;34mSome\033[0m") != std::string::npos);
    CML_ASSERT(ansi.find("\033[33m42\033[0m") != std::string::npos);
    CML_ASSERT(ansi.find("\033[32m\"hi\"\033[0m") != std::string::npos);
}

CML_TEST(platform_compile_to_bytecode_with_imports) {
    Pipeline p;
    std::string code = R"(
let math = import "std/math"
type Shape = Circle Float | Rect Float Float
let area s =
  match s with
  | Circle r -> math.pi *. r *. r
  | Rect w h -> w *. h
int_of_float (math.round (area (Rect 6.0 7.0) +. math.sqrt 9.0))
)";
    BytecodeProgram bp = p.compile_to_bytecode("shape.cml", code);
    std::string raw = serialize_bytecode(bp);
    std::string wrapped = "#!/bin/sh\nexec commonml --run-bc \"$0\" \"$@\"\n" + raw;
    BytecodeProgram decoded = deserialize_bytecode(wrapped);

    VM vm;
    for (const auto& c : decoded.ctors) {
        if (c.arity == 0) {
            vm.set_global(c.name, Value::make_adt(c.tag, c.name, {}));
        } else {
            vm.set_global(c.name, Value{ValCtor{c.tag, c.name, c.arity, {}}});
        }
    }
    Value out = vm.run(decoded.entry);
    CML_ASSERT_EQ(value_to_string(out), std::string("45"));
}

CML_TEST(platform_toolchain_compile_output_and_relocatable_stdlib) {
    TempDirGuard tmp("cml_toolchain_test");
    fs::path prefix = tmp.path / "install_prefix";
    fs::create_directories(prefix / "bin");
    fs::create_directories(prefix / "share" / "commonml" / "stdlib");
    {
        std::ofstream m(prefix / "share" / "commonml" / "stdlib" / "tinymath.cml");
        m << "let triple x = x * 3\n";
    }
    set_executable_path((prefix / "bin" / "cml").string());
    Pipeline reloc_p;
    EvalResult r = reloc_p.eval_source("reloc.cml", "let tm = import \"std/tinymath\"\ntm.triple 14\n");
    CML_ASSERT_EQ(value_to_string(r.value), std::string("42"));
    set_executable_path("");

    fs::path src_file = tmp.path / "app.cml";
    fs::path out_exe = tmp.path / "app";
    {
        std::ofstream f(src_file);
        f << "let math = import \"std/math\"\nmath.factorial 6\n";
    }
    CompileOutputOptions co;
    co.input_file = src_file.string();
    co.output_file = out_exe.string();
    co.fallback_runtime = "commonml";
    std::ostringstream out;
    std::ostringstream err;
    CML_ASSERT_EQ(compile_file_to_output(co, out, err), 0);
    CML_ASSERT(fs::exists(out_exe));
}

CML_TEST(platform_structs_indentation_and_three_loops) {
    Pipeline p;
    EvalResult res = p.eval_source("loops_structs.cml", R"(
struct Point
  x : Float
  y : Float

struct Box a
  value : a

let sum_to n =
  var acc = 0
  var i = 1
  while i <= n
    if i == 3
      i = i + 1
      continue
    if i == 7
      break
    acc = acc + i
    i = i + 1
  acc

let for_c_sum _ =
  var s = 0
  for (var i = 0; i < 6; i = i + 1)
    if i == 2
      continue
    s = s + i
  s

let for_each_sum xs =
  var s = 0
  for each x in xs
    s = s + x
  s

let p1 = Point { x = 1.5, y = 2.5 }
p1.x = 10.5
let b = Box { value = 42 }
(sum_to 10, for_c_sum (), for_each_sum [1, 2, 3, 4], int_of_float (p1.x +. p1.y), b.value)
)");
    CML_ASSERT_EQ(value_to_string(res.value), std::string("(18, 13, 10, 13, 42)"));

    // Immutable `let` cannot be mutated
    Pipeline p2;
    CML_ASSERT_THROWS(p2.eval_source("immut.cml", "let x = 1\nx = 2\n"), CompileError);
}

CML_TEST(platform_rc_collections_and_file_ops) {
    TempDirGuard tmp("cml_fs_test");
    Pipeline p;
    std::string code = "let base = \"" + tmp.path.string() + "\"\n" + R"(
-- Reference counting check
let a = [1, 2, 3]
let b = a
let rc_aliased = rc_count a

-- Vector
let v = vec_new ()
vec_push v 10
vec_push v 20
vec_set v 0 15
let v_pop = vec_pop v
var v_sum = 0
for each item in v
  v_sum = v_sum + item

-- Dict
let d = dict_new ()
dict_set d "alpha" 100
dict_set d "beta" 200
let _ = dict_remove d "alpha"
var d_sum = 0
for each (k, val) in d
  if k == "beta"
    d_sum = d_sum + val

-- Set
let s = set_new ()
let _ = set_add s "x"
let _ = set_add s "x"
let _ = set_add s "y"
var s_cnt = 0
for each _ in s
  s_cnt = s_cnt + 1

-- File operations
let sub = base ++ "/sub"
make_dir sub
let f1 = sub ++ "/hello.txt"
let f2 = sub ++ "/copy.txt"
write_file f1 "hello "
append_file f1 "world"
copy_file f1 f2
let h = file_open f2 "r"
let line = file_read_line h
file_close h
let files = list_dir sub
let sz = file_size f2
let removed = delete_file f1
(rc_aliased >= 2, v_pop, v_sum, d_sum, set_len s, s_cnt, line, list_len files, sz, removed, is_dir sub)
)";
    EvalResult res = p.eval_source("coll_fs.cml", code);
    CML_ASSERT_EQ(value_to_string(res.value),
                  std::string("(true, 20, 15, 200, 2, 2, \"hello world\", 2, 11, true, true)"));
}

CML_TEST(platform_systems_structs_pointers_defer_and_numerics) {
    Pipeline p;
    EvalResult res = p.eval_source("systems_test.cml", R"(
let rect = struct:
  x: i32
  y: i32
  name: str
  ok: bool

let var_rect = new rect { .x = 10, .y = 20, .name = "main", .ok = true }
let area = fn() : i32:
  var_rect->x * var_rect->y

let live_before_close = heap_live_count ()
let a1 = area ()
var_rect->x = 15
let a2 = area ()
close var_rect
let live_after_close = heap_live_count ()

-- Stack struct value accessed with `.`, pointer accessed with `->`
let r_val = rect { .x = 3, .y = 4, .name = "stack", .ok = false }
let r_ptr = &r_val
r_ptr->y = 5
let stack_area = r_val.x * r_val.y

-- Pointer dereference `*` and `&` on mutable scalar + defer LIFO cleanup
let calc_with_defer = fn() : i32:
  let h = new rect { .x = 6, .y = 7, .name = "scoped", .ok = true }
  defer close h
  var counter : i32 = 10
  let cp = &counter
  *cp = *cp + 5
  (h->x * h->y) + counter

let scoped_val = calc_with_defer ()
let live_after_defer = heap_live_count ()

-- Fixed-width numerics, strings, booleans, bitwise ops, and uniform `=` blocks
let mask : u32 = (0 | 5) ^ 3
let shifted : i64 = (3 << 4) >> 1
let bits_ok : bool = (!false) && ((mask & 6) == 6) && (~0 == -1)
let greeting : string = r_val.name ++ "_ok"

(a1, a2, live_before_close, live_after_close, stack_area, scoped_val, live_after_defer, mask, shifted, bits_ok, greeting)
)");
    CML_ASSERT_EQ(
        value_to_string(res.value),
        std::string("(200, 300, 1, 0, 15, 57, 0, 6, 24, true, \"stack_ok\")"));

    // Safety checks:
    // 1. Using `.` on pointer type is a type error (`->` required)
    Pipeline p_dot_err;
    CML_ASSERT_THROWS(p_dot_err.eval_source("dot_err.cml", R"(
let rect = struct:
  x: i32
  y: i32
let r = new rect { .x = 1, .y = 2 }
r.x
)"),
                      CompileError);

    // 2. Use-after-close and double-close throw runtime safety errors
    Pipeline p_uaf;
    CML_ASSERT_THROWS(p_uaf.eval_source("uaf.cml", R"(
let rect = struct:
  x: i32
  y: i32
let r = new rect { .x = 1, .y = 2 }
close r
r->x
)"),
                      CompileError);

    Pipeline p_dbl;
    CML_ASSERT_THROWS(p_dbl.eval_source("dbl.cml", R"(
let p = new 42
close p
close p
)"),
                      CompileError);

    // 3. Width mismatch between i32 and i64 is rejected statically
    Pipeline p_width;
    CML_ASSERT_THROWS(p_width.eval_source("width.cml", R"(
let a : i32 = 1
let b : i64 = 2
a + b
)"),
                      CompileError);
}

CML_TEST(platform_debugger_cli_and_dap) {
    TempDirGuard tmp("cml_dbg_test");
    fs::path dbg_file = tmp.path / "dbg_sample.cml";
    {
        std::ofstream f(dbg_file);
        f << "let rect = struct:\n"
          << "  x: i32\n"
          << "  y: i32\n"
          << "\n"
          << "let calc a =\n"
          << "  let r = new rect { .x = a, .y = 5 }\n"
          << "  defer close r\n"
          << "  let total = r->x * r->y\n"
          << "  total\n"
          << "\n"
          << "calc 12\n";
    }

    DebugSessionConfig cfg;
    cfg.entry_file = dbg_file.string();
    cfg.stop_on_entry = false;
    cfg.initial_breakpoints = {8};

    std::istringstream in("locals\np r->x\nheap\nbt\nc\n");
    std::ostringstream out;
    std::ostringstream err;
    Debugger dbg;
    int rc = dbg.run_cli(cfg, in, out, err);
    CML_ASSERT_EQ(rc, 0);
    std::string out_s = out.str();
    CML_ASSERT(out_s.find("[breakpoint]") != std::string::npos);
    CML_ASSERT(out_s.find("a = 12") != std::string::npos);
    CML_ASSERT(out_s.find("12\n") != std::string::npos);
    CML_ASSERT(out_s.find("live heap allocations: 1") != std::string::npos);
    CML_ASSERT(out_s.find("Program exited with value: 60") != std::string::npos);

    // DAP initialize + disconnect roundtrip
    std::string m1 = "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\"}";
    std::string m2 = "{\"seq\":2,\"type\":\"request\",\"command\":\"disconnect\"}";
    std::istringstream dap_in("Content-Length: " + std::to_string(m1.size()) + "\r\n\r\n" + m1 +
                               "Content-Length: " + std::to_string(m2.size()) + "\r\n\r\n" + m2);
    std::ostringstream dap_out;
    std::ostringstream dap_err;
    CML_ASSERT_EQ(dbg.run_dap(dap_in, dap_out, dap_err), 0);
    std::string dap_s = dap_out.str();
    CML_ASSERT(dap_s.find("\"command\":\"initialize\"") != std::string::npos);
    CML_ASSERT(dap_s.find("\"event\":\"initialized\"") != std::string::npos);
    CML_ASSERT(dap_s.find("\"command\":\"disconnect\"") != std::string::npos);
}
