#include "cml/toolchain.hpp"
#include "cml/bytecode.hpp"
#include "cml/package.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace cml {

namespace fs = std::filesystem;

std::string resolve_runtime_binary(const std::string& argv0, const std::string& compile_fallback) {
    std::error_code ec;
    if (!argv0.empty()) {
        fs::path self = fs::weakly_canonical(argv0, ec);
        if (!ec) {
            fs::path sibling = self.parent_path() / "commonml";
            if (fs::exists(sibling, ec)) return sibling.string();
            fs::path cml_sibling = self.parent_path() / "cml";
            if (fs::exists(cml_sibling, ec)) return cml_sibling.string();
        }
    }
    if (!compile_fallback.empty() && fs::exists(compile_fallback, ec)) {
        return compile_fallback;
    }
    return "commonml";
}

int compile_file_to_output(const CompileOutputOptions& opts, std::ostream& out, std::ostream& err) {
    std::ifstream ifs(opts.input_file);
    if (!ifs) {
        err << "error: could not open file '" << opts.input_file << "'\n";
        return 1;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string code = ss.str();

    std::string out_path = opts.output_file;
    bool bc_only = opts.emit_bc_only;
    if (out_path.empty()) {
        fs::path in_p(opts.input_file);
        if (bc_only) {
            out_path = (in_p.parent_path() / (in_p.stem().string() + ".cmlbc")).string();
        } else if (in_p.extension() == ".cml") {
            out_path = (in_p.parent_path() / in_p.stem()).string();
        } else {
            out_path = opts.input_file + ".out";
        }
    } else if (fs::path(out_path).extension() == ".cmlbc") {
        bc_only = true;
    }

    Pipeline pipeline;
    std::string manifest = PackageManager::find_manifest(opts.input_file);
    if (!manifest.empty()) {
        try {
            PackageManager::configure_pipeline(pipeline,
                                               fs::path(manifest).parent_path().string());
        } catch (const CompileError&) {
        }
    }

    try {
        std::string dbg;
        BytecodeProgram bp = pipeline.compile_to_bytecode(opts.input_file, code, opts.eval_opts, &dbg);
        if (!dbg.empty()) {
            out << dbg;
        }
        std::string bc = serialize_bytecode(bp);

        std::ofstream ofs(out_path, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            err << "error: could not open output file '" << out_path << "'\n";
            return 1;
        }

        if (bc_only) {
            ofs.write(bc.data(), static_cast<std::streamsize>(bc.size()));
        } else {
            ofs << "#!/bin/sh\n"
                << "RUNTIME=\"${CML_RUNTIME:-}\"\n"
                << "if [ -z \"$RUNTIME\" ]; then\n"
                << "  if command -v commonml >/dev/null 2>&1; then\n"
                << "    RUNTIME=\"$(command -v commonml)\"\n"
                << "  elif command -v cml >/dev/null 2>&1; then\n"
                << "    RUNTIME=\"$(command -v cml)\"\n"
                << "  else\n"
                << "    RUNTIME=\"" << opts.fallback_runtime << "\"\n"
                << "  fi\n"
                << "fi\n"
                << "exec \"$RUNTIME\" --run-bc \"$0\" \"$@\"\n";
            ofs.write(bc.data(), static_cast<std::streamsize>(bc.size()));
        }
        ofs.close();

        if (!bc_only) {
            std::error_code ec;
            fs::permissions(out_path,
                            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, ec);
        }
    } catch (const CompileError& e) {
        SourceFile sf(opts.input_file, code);
        err << e.diagnostic().format(&sf);
        return 1;
    }

    return 0;
}

}  // namespace cml
