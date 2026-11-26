#include "cml/package.hpp"
#include "cml/builtins.hpp"
#include "cml/source.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cml {

namespace fs = std::filesystem;

namespace {

std::string default_entry_for_kind(const std::string& kind) {
    return kind == "lib" ? "src/lib.cml" : "src/main.cml";
}

void validate_kind(const std::string& kind) {
    if (kind != "lib" && kind != "console" && kind != "web") {
        throw CompileError(Diagnostic{
            "pkg", "invalid project kind '" + kind + "' (expected lib, console, or web)", {}});
    }
}

void write_text_file(const fs::path& p, const std::string& content) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        throw CompileError(Diagnostic{"pkg", "could not write file '" + p.string() + "'", {}});
    }
    ofs << content;
}

std::string template_lib_source(const std::string& name) {
    return "-- " + name + " library\n"
           "let math = import \"std/math\"\n\n"
           "let Stats = struct:\n"
           "              count : Int\n"
           "              total : Float\n"
           "              avg : Float\n\n"
           "let summarize xs =\n"
           "  let n = list_len xs\n"
           "  let total = math.sum_float xs\n"
           "  let avg = if n == 0 then 0.0 else total /. float_of_int n\n"
           "  Stats { count = n, total = total, avg = avg }\n\n"
           "let greet who = \"Hello from " + name + ", \" ++ who ++ \"!\"\n";
}

std::string template_lib_test(const std::string& name) {
    return "-- Tests for " + name + "\n"
           "let lib = import \"../src/lib.cml\"\n"
           "let math = import \"std/math\"\n\n"
           "let s = lib.summarize [2.0, 4.0, 6.0]\n"
           "let _ = if s.count != 3 then panic \"expected count 3\" else ()\n"
           "let _ = if math.fabs (s.total -. 12.0) > 0.0001 then panic \"expected total 12.0\" else ()\n"
           "let _ = if math.fabs (s.avg -. 4.0) > 0.0001 then panic \"expected avg 4.0\" else ()\n"
           "let _ = println (lib.greet \"CommonML\")\n"
           "\"ok\"\n";
}

std::string template_console_source(const std::string& name) {
    return "-- " + name + " console application\n"
           "let con = import \"std/console\"\n"
           "let math = import \"std/math\"\n\n"
           "let args = cli_args ()\n"
           "let _ = con.banner \"" + name + " CLI\"\n"
           "let _ = println (\"pi = \" ++ to_string math.pi ++ \", sqrt(2) = \" ++ to_string (math.sqrt 2.0))\n"
           "let _ =\n"
           "  if is_empty args then\n"
           "    println \"Pass arguments with: cml run -- arg1 arg2\"\n"
           "  else\n"
           "    println (\"Args: \" ++ to_string args)\n"
           "0\n";
}

std::string template_console_test(const std::string& name) {
    return "-- Tests for " + name + "\n"
           "let math = import \"std/math\"\n\n"
           "let _ = if math.gcd 48 18 != 6 then panic \"gcd test failed\" else ()\n"
           "println \"console app tests passed\"\n"
           "\"ok\"\n";
}

std::string template_web_source(const std::string& name) {
    return "-- " + name + " web application\n"
           "let http = import \"std/http\"\n"
           "let math = import \"std/math\"\n\n"
           "let router req =\n"
           "  match req with\n"
           "  | Req GET \"/\" _ _ ->\n"
           "      http.ok_html (\"<h1>Welcome to " + name + "</h1><p>Powered by CommonML</p>\")\n"
           "  | Req GET \"/health\" _ _ ->\n"
           "      http.ok_json \"{\\\"status\\\":\\\"ok\\\"}\"\n"
           "  | Req GET \"/pi\" _ _ ->\n"
           "      http.ok_json (\"{\\\"pi\\\":\" ++ to_string math.pi ++ \"}\")\n"
           "  | Req _ path _ _ ->\n"
           "      http.not_found (\"Route not found: \" ++ path)\n\n"
           "let env_port = env_get \"PORT\"\n"
           "let port = if is_valid_int env_port then int_of_string env_port else 8080\n"
           "let _ = println (\"Starting " + name + " on http://127.0.0.1:\" ++ to_string port)\n"
           "http.http_serve \"127.0.0.1\" port router\n";
}

std::string template_web_test(const std::string& name) {
    return "-- Tests for " + name + "\n"
           "let http = import \"std/http\"\n\n"
           "let req = http.parse_request \"GET /health HTTP/1.1\\r\\nHost: localhost\\r\\n\\r\\n\"\n"
           "let res =\n"
           "  match req with\n"
           "  | Req GET \"/health\" _ _ -> http.ok_json \"{\\\"status\\\":\\\"ok\\\"}\"\n"
           "  | _ -> http.not_found \"not found\"\n"
           "let wire = http.format_response res\n"
           "let _ = if not (str_starts_with wire \"HTTP/1.1 200 OK\") then panic \"bad http response\" else ()\n"
           "println \"web app tests passed\"\n"
           "\"ok\"\n";
}

PackageManifest load_build_cml(const std::string& build_path) {
    PackageManifest m;
    Pipeline p;

    StructInfo builder_info;
    builder_info.name = "Builder";
    builder_info.fields = {
        {"set_name", make_tfun(string_type(), unit_type())},
        {"set_version", make_tfun(string_type(), unit_type())},
        {"set_kind", make_tfun(string_type(), unit_type())},
        {"set_entry", make_tfun(string_type(), unit_type())},
        {"add_dep", make_tfun(string_type(), make_tfun(string_type(), unit_type()))},
    };
    p.typechecker_mut().register_struct_info(builder_info);

    std::unordered_map<std::string, Value> b_fields;
    b_fields["set_name"] = Value{ValNative{
        "set_name", 1, {}, [&m](const std::vector<Value>& args) {
            m.name = std::get<ValString>(args[0].node).value;
            return Value::unit();
        }}};
    b_fields["set_version"] = Value{ValNative{
        "set_version", 1, {}, [&m](const std::vector<Value>& args) {
            m.version = std::get<ValString>(args[0].node).value;
            return Value::unit();
        }}};
    b_fields["set_kind"] = Value{ValNative{
        "set_kind", 1, {}, [&m](const std::vector<Value>& args) {
            m.kind = std::get<ValString>(args[0].node).value;
            return Value::unit();
        }}};
    b_fields["set_entry"] = Value{ValNative{
        "set_entry", 1, {}, [&m](const std::vector<Value>& args) {
            m.entry = std::get<ValString>(args[0].node).value;
            return Value::unit();
        }}};
    b_fields["add_dep"] = Value{ValNative{
        "add_dep", 2, {}, [&m](const std::vector<Value>& args) {
            std::string dname = std::get<ValString>(args[0].node).value;
            std::string dpath = std::get<ValString>(args[1].node).value;
            m.dependencies.push_back(PackageDep{std::move(dname), std::move(dpath), "0.1.0"});
            return Value::unit();
        }}};

    Value builder_val = Value::make_struct("Builder", std::move(b_fields));
    p.eval_file(build_path);

    if (const Value* build_fn = p.vm().get_global("build")) {
        p.vm_mut().call_value(*build_fn, {builder_val}, {});
    } else {
        if (const Value* v = p.vm().get_global("name")) {
            if (auto* s = std::get_if<ValString>(&v->node)) m.name = s->value;
        }
        if (const Value* v = p.vm().get_global("version")) {
            if (auto* s = std::get_if<ValString>(&v->node)) m.version = s->value;
        }
        if (const Value* v = p.vm().get_global("kind")) {
            if (auto* s = std::get_if<ValString>(&v->node)) m.kind = s->value;
        }
        if (const Value* v = p.vm().get_global("entry")) {
            if (auto* s = std::get_if<ValString>(&v->node)) m.entry = s->value;
        }
    }

    if (m.name.empty()) {
        throw CompileError(Diagnostic{"pkg", "build.cml did not set package name", {}});
    }
    validate_kind(m.kind);
    if (m.entry.empty()) {
        m.entry = default_entry_for_kind(m.kind);
    }
    return m;
}

}  // namespace

PackageManifest PackageManager::load_manifest(const std::string& manifest_path) {
    return load_build_cml(manifest_path);
}

void PackageManager::save_manifest(const std::string& manifest_path, const PackageManifest& m) {
    std::ostringstream os;
    os << "-- CommonML project build configuration\n"
       << "let build (b : Builder) =\n"
       << "  b.set_name \"" << m.name << "\"\n"
       << "  b.set_version \"" << m.version << "\"\n"
       << "  b.set_kind \"" << m.kind << "\"\n"
       << "  b.set_entry \"" << m.entry << "\"\n";
    for (const auto& d : m.dependencies) {
        os << "  b.add_dep \"" << d.name << "\" \"" << d.path << "\"\n";
    }
    write_text_file(manifest_path, os.str());
}

std::string PackageManager::find_manifest(const std::string& start_dir) {
    std::error_code ec;
    fs::path cur = fs::weakly_canonical(start_dir, ec);
    if (ec) cur = fs::path(start_dir);
    if (fs::is_regular_file(cur, ec)) {
        cur = cur.parent_path();
    }
    while (!cur.empty()) {
        fs::path build_cml = cur / "build.cml";
        if (fs::exists(build_cml, ec) && fs::is_regular_file(build_cml, ec)) {
            return build_cml.string();
        }
        fs::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return "";
}

static fs::path manifest_in_dir(const fs::path& dir) {
    std::error_code ec;
    fs::path build_cml = dir / "build.cml";
    if (fs::exists(build_cml, ec)) return build_cml;
    return {};
}

std::vector<ResolvedPackage> PackageManager::resolve_dependencies(const std::string& project_dir) {
    std::vector<ResolvedPackage> out;
    std::unordered_map<std::string, std::string> name_to_root;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> visiting;

    auto visit = [&](auto& self, const fs::path& dir, bool is_root) -> void {
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(dir, ec);
        if (ec) canon = dir;
        std::string key = canon.string();

        if (visited.count(key)) return;
        if (visiting.count(key)) {
            throw CompileError(Diagnostic{"pkg", "circular package dependency at '" + key + "'", {}});
        }

        fs::path manifest_path = manifest_in_dir(canon);
        if (manifest_path.empty()) {
            throw CompileError(Diagnostic{"pkg", "missing build.cml in '" + key + "'", {}});
        }
        PackageManifest m = load_manifest(manifest_path.string());

        auto existing = name_to_root.find(m.name);
        if (existing != name_to_root.end() && existing->second != key) {
            throw CompileError(Diagnostic{
                "pkg", "package name conflict for '" + m.name + "' at " + key, {}});
        }
        name_to_root[m.name] = key;

        visiting.insert(key);
        std::vector<std::string> dep_names;
        for (const auto& dep : m.dependencies) {
            fs::path dep_dir = fs::path(dep.path);
            if (dep_dir.is_relative()) {
                dep_dir = canon / dep_dir;
            }
            self(self, dep_dir, false);
            dep_names.push_back(dep.name);
        }
        visiting.erase(key);
        visited.insert(key);

        if (!is_root) {
            fs::path entry_abs = canon / m.entry;
            out.push_back(ResolvedPackage{
                m.name,
                m.version,
                m.kind,
                canon.string(),
                entry_abs.string(),
                std::move(dep_names),
            });
        }
    };

    visit(visit, fs::path(project_dir), true);
    return out;
}

void PackageManager::write_lockfile(const std::string& project_dir,
                                    const std::vector<ResolvedPackage>& resolved) {
    std::ostringstream os;
    os << "# Auto-generated by CommonML package manager\n";
    for (const auto& pkg : resolved) {
        os << "[[package]]\n"
           << "name = \"" << pkg.name << "\"\n"
           << "version = \"" << pkg.version << "\"\n"
           << "kind = \"" << pkg.kind << "\"\n"
           << "path = \"" << pkg.root_dir << "\"\n"
           << "entry = \"" << pkg.entry_file << "\"\n\n";
    }
    write_text_file(fs::path(project_dir) / "cml.lock", os.str());
}

void PackageManager::configure_pipeline(Pipeline& pipeline, const std::string& project_dir) {
    fs::path root(project_dir);
    pipeline.add_search_path((root / "src").string());
    pipeline.add_search_path(root.string());

    auto deps = resolve_dependencies(project_dir);
    for (const auto& dep : deps) {
        pipeline.register_package_entry(dep.name, dep.entry_file);
        pipeline.add_search_path((fs::path(dep.root_dir) / "src").string());
        pipeline.add_search_path(dep.root_dir);
    }
}

void PackageManager::create_project(const std::string& target_dir, const std::string& kind,
                                    const std::string& name) {
    validate_kind(kind);
    if (name.empty()) {
        throw CompileError(Diagnostic{"pkg", "project name cannot be empty", {}});
    }
    std::error_code ec;
    if (fs::exists(target_dir, ec)) {
        throw CompileError(Diagnostic{"pkg", "directory already exists: '" + target_dir + "'", {}});
    }
    fs::create_directories(fs::path(target_dir) / "src", ec);
    fs::create_directories(fs::path(target_dir) / "tests", ec);

    PackageManifest m;
    m.name = name;
    m.version = "0.1.0";
    m.kind = kind;
    m.entry = default_entry_for_kind(kind);
    save_manifest((fs::path(target_dir) / "build.cml").string(), m);

    if (kind == "lib") {
        write_text_file(fs::path(target_dir) / "src" / "lib.cml", template_lib_source(name));
        write_text_file(fs::path(target_dir) / "tests" / "test_main.cml", template_lib_test(name));
    } else if (kind == "console") {
        write_text_file(fs::path(target_dir) / "src" / "main.cml", template_console_source(name));
        write_text_file(fs::path(target_dir) / "tests" / "test_main.cml",
                        template_console_test(name));
    } else {
        write_text_file(fs::path(target_dir) / "src" / "main.cml", template_web_source(name));
        write_text_file(fs::path(target_dir) / "tests" / "test_main.cml", template_web_test(name));
    }

    write_text_file(fs::path(target_dir) / "README.md",
                    "# " + name + "\n\nCommonML `" + kind + "` project.\n\n"
                    "```sh\ncml build\ncml test\ncml run\n```\n");
}

void PackageManager::init_project(const std::string& target_dir, const std::string& kind) {
    validate_kind(kind);
    fs::path dir(target_dir);
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(dir, ec);
    std::string name = (ec ? dir : canon).filename().string();
    if (name.empty()) name = "app";

    fs::path manifest_path = dir / "build.cml";
    if (fs::exists(manifest_path, ec)) {
        throw CompileError(
            Diagnostic{"pkg", "build.cml already exists in '" + target_dir + "'", {}});
    }

    PackageManifest m;
    m.name = name;
    m.version = "0.1.0";
    m.kind = kind;
    m.entry = default_entry_for_kind(kind);
    save_manifest(manifest_path.string(), m);

    fs::path entry_path = dir / m.entry;
    if (!fs::exists(entry_path, ec)) {
        if (kind == "lib") {
            write_text_file(entry_path, template_lib_source(name));
        } else if (kind == "console") {
            write_text_file(entry_path, template_console_source(name));
        } else {
            write_text_file(entry_path, template_web_source(name));
        }
    }
    fs::path test_path = dir / "tests" / "test_main.cml";
    if (!fs::exists(test_path, ec)) {
        if (kind == "lib") write_text_file(test_path, template_lib_test(name));
        else if (kind == "console") write_text_file(test_path, template_console_test(name));
        else write_text_file(test_path, template_web_test(name));
    }
}

void PackageManager::add_dependency(const std::string& project_dir, const std::string& dep_name,
                                    const std::string& dep_path) {
    fs::path manifest_path = manifest_in_dir(fs::path(project_dir));
    if (manifest_path.empty()) {
        throw CompileError(Diagnostic{"pkg", "no build.cml found in '" + project_dir + "'", {}});
    }
    PackageManifest m = load_manifest(manifest_path.string());

    fs::path resolved_dep = fs::path(dep_path);
    if (resolved_dep.is_relative()) {
        resolved_dep = fs::path(project_dir) / resolved_dep;
    }
    fs::path dep_manifest = manifest_in_dir(resolved_dep);
    if (dep_manifest.empty()) {
        throw CompileError(
            Diagnostic{"pkg", "dependency missing build.cml in '" + resolved_dep.string() + "'", {}});
    }
    PackageManifest dep_m = load_manifest(dep_manifest.string());
    std::string actual_name = dep_name.empty() ? dep_m.name : dep_name;

    bool updated = false;
    for (auto& d : m.dependencies) {
        if (d.name == actual_name) {
            d.path = dep_path;
            d.version = dep_m.version;
            updated = true;
            break;
        }
    }
    if (!updated) {
        m.dependencies.push_back(PackageDep{actual_name, dep_path, dep_m.version});
    }
    save_manifest(manifest_path.string(), m);
    auto resolved = resolve_dependencies(project_dir);
    write_lockfile(project_dir, resolved);
}

int PackageManager::cmd_install(const std::string& project_dir, std::ostream& out) {
    fs::path mp = manifest_in_dir(fs::path(project_dir));
    PackageManifest m = load_manifest(mp.string());
    auto resolved = resolve_dependencies(project_dir);
    write_lockfile(project_dir, resolved);
    out << "Installed " << resolved.size() << " dependency package(s) for '" << m.name << "'\n";
    return 0;
}

int PackageManager::cmd_build(const std::string& project_dir, std::ostream& out) {
    fs::path mp = manifest_in_dir(fs::path(project_dir));
    PackageManifest m = load_manifest(mp.string());
    auto resolved = resolve_dependencies(project_dir);
    write_lockfile(project_dir, resolved);

    Pipeline pipeline;
    configure_pipeline(pipeline, project_dir);

    EvalOptions opts;
    opts.typecheck_only = true;
    fs::path entry = fs::path(project_dir) / m.entry;
    pipeline.eval_file(entry.string(), opts);
    out << "Built " << m.kind << " package '" << m.name << "' v" << m.version
        << " (" << entry.string() << ")\n";
    return 0;
}

int PackageManager::cmd_run(const std::string& project_dir, const std::vector<std::string>& args,
                            std::ostream& out, std::ostream& err) {
    fs::path mp = manifest_in_dir(fs::path(project_dir));
    PackageManifest m = load_manifest(mp.string());
    if (m.kind == "lib") {
        err << "error: package '" << m.name
            << "' has kind 'lib' and cannot be run directly; use 'cml test'\n";
        return 1;
    }
    set_cli_args(args);
    Pipeline pipeline;
    configure_pipeline(pipeline, project_dir);
    fs::path entry = fs::path(project_dir) / m.entry;
    EvalResult res = pipeline.eval_file(entry.string(), EvalOptions{});
    (void)out;
    (void)res;
    return 0;
}

int PackageManager::cmd_test(const std::string& project_dir, std::ostream& out, std::ostream& err) {
    fs::path mp = manifest_in_dir(fs::path(project_dir));
    PackageManifest m = load_manifest(mp.string());
    fs::path tests_dir = fs::path(project_dir) / "tests";
    std::error_code ec;
    if (!fs::exists(tests_dir, ec) || !fs::is_directory(tests_dir, ec)) {
        out << "No tests directory found for '" << m.name << "'\n";
        return 0;
    }

    std::vector<fs::path> test_files;
    for (const auto& entry : fs::directory_iterator(tests_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cml") {
            test_files.push_back(entry.path());
        }
    }
    std::sort(test_files.begin(), test_files.end());

    int passed = 0;
    int failed = 0;
    for (const auto& tf : test_files) {
        Pipeline pipeline;
        configure_pipeline(pipeline, project_dir);
        try {
            pipeline.eval_file(tf.string(), EvalOptions{});
            out << "[PASS] " << tf.filename().string() << "\n";
            ++passed;
        } catch (const CompileError& e) {
            err << "[FAIL] " << tf.filename().string() << ": " << e.diagnostic().message << "\n";
            ++failed;
        }
    }
    out << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

int PackageManager::cmd_list(const std::string& project_dir, std::ostream& out) {
    fs::path mp = manifest_in_dir(fs::path(project_dir));
    PackageManifest m = load_manifest(mp.string());
    auto resolved = resolve_dependencies(project_dir);
    out << m.name << " v" << m.version << " [" << m.kind << "] (" << m.entry << ")\n";
    if (resolved.empty()) {
        out << "  (no dependencies)\n";
    } else {
        for (const auto& dep : resolved) {
            out << "  - " << dep.name << " v" << dep.version << " [" << dep.kind << "] -> "
                << dep.root_dir << "\n";
        }
    }
    return 0;
}

}  // namespace cml
