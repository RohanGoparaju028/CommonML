#pragma once

#include "cml/pipeline.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace cml {

struct PackageDep {
    std::string name;
    std::string path;
    std::string version = "*";
};

struct PackageManifest {
    std::string name;
    std::string version = "0.1.0";
    std::string kind = "console";  // "lib" | "console" | "web"
    std::string entry = "src/main.cml";
    std::vector<PackageDep> dependencies;
};

struct ResolvedPackage {
    std::string name;
    std::string version;
    std::string kind;
    std::string root_dir;
    std::string entry_file;
    std::vector<std::string> dep_names;
};

class PackageManager {
public:
    static PackageManifest load_manifest(const std::string& manifest_path);
    static void save_manifest(const std::string& manifest_path, const PackageManifest& manifest);

    static std::string find_manifest(const std::string& start_dir = ".");

    static std::vector<ResolvedPackage> resolve_dependencies(const std::string& project_dir);
    static void write_lockfile(const std::string& project_dir,
                               const std::vector<ResolvedPackage>& resolved);

    static void configure_pipeline(Pipeline& pipeline, const std::string& project_dir);

    static void create_project(const std::string& target_dir, const std::string& kind,
                               const std::string& name);
    static void init_project(const std::string& target_dir, const std::string& kind);
    static void add_dependency(const std::string& project_dir, const std::string& dep_name,
                               const std::string& dep_path);

    static int cmd_install(const std::string& project_dir, std::ostream& out);
    static int cmd_build(const std::string& project_dir, std::ostream& out);
    static int cmd_run(const std::string& project_dir, const std::vector<std::string>& args,
                       std::ostream& out, std::ostream& err);
    static int cmd_test(const std::string& project_dir, std::ostream& out, std::ostream& err);
    static int cmd_list(const std::string& project_dir, std::ostream& out);
};

}  // namespace cml
