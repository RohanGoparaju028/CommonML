#include "builtins_internal.hpp"
#include "cml/bytecode.hpp"
#include "cml/source.hpp"
#include "cml/vm.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace cml::detail {

void append_string_list_builtins(std::vector<BuiltinDef>& list) {
    auto tv = [](uint32_t id) { return make_tvar(id); };

    // str_byte_at : String -> Int -> Int
    list.push_back(BuiltinDef{
        "str_byte_at",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(int_type(), int_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& s = std::get<ValString>(args[0].node).value;
            int64_t idx = std::get<ValInt>(args[1].node).value;
            if (idx < 0 || static_cast<size_t>(idx) >= s.size()) {
                return Value::make_int(-1);
            }
            return Value::make_int(static_cast<unsigned char>(s[static_cast<size_t>(idx)]));
        },
    });

    // char_from_code : Int -> String
    list.push_back(BuiltinDef{
        "char_from_code",
        TypeScheme{{}, make_tfun(int_type(), string_type())},
        1,
        [](const std::vector<Value>& args) {
            int64_t code = std::get<ValInt>(args[0].node).value;
            if (code < 0 || code > 255) {
                throw CompileError(Diagnostic{"runtime", "char_from_code out of byte range 0..255", {}});
            }
            return Value::make_string(std::string(1, static_cast<char>(code)));
        },
    });

    // str_concat_list : List String -> String
    list.push_back(BuiltinDef{
        "str_concat_list",
        TypeScheme{{}, make_tfun(list_type(string_type()), string_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& elems = std::get<ValList>(args[0].node).ptr->elems;
            std::string out;
            for (const auto& e : elems) {
                out += std::get<ValString>(e.node).value;
            }
            return Value::make_string(std::move(out));
        },
    });

    // str_index_of : String -> String -> Int
    list.push_back(BuiltinDef{
        "str_index_of",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), int_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& haystack = std::get<ValString>(args[0].node).value;
            const auto& needle = std::get<ValString>(args[1].node).value;
            size_t pos = haystack.find(needle);
            return Value::make_int(pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
        },
    });

    // str_split : String -> String -> List String
    list.push_back(BuiltinDef{
        "str_split",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), list_type(string_type())))},
        2,
        [](const std::vector<Value>& args) {
            const auto& s = std::get<ValString>(args[0].node).value;
            const auto& delim = std::get<ValString>(args[1].node).value;
            std::vector<Value> parts;
            if (delim.empty()) {
                for (char c : s) {
                    parts.push_back(Value::make_string(std::string(1, c)));
                }
                return Value::make_list(std::move(parts));
            }
            size_t start = 0;
            while (true) {
                size_t pos = s.find(delim, start);
                if (pos == std::string::npos) {
                    parts.push_back(Value::make_string(s.substr(start)));
                    break;
                }
                parts.push_back(Value::make_string(s.substr(start, pos - start)));
                start = pos + delim.size();
            }
            return Value::make_list(std::move(parts));
        },
    });

    // str_trim : String -> String
    list.push_back(BuiltinDef{
        "str_trim",
        TypeScheme{{}, make_tfun(string_type(), string_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& s = std::get<ValString>(args[0].node).value;
            size_t begin = 0;
            while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
            size_t end = s.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
            return Value::make_string(s.substr(begin, end - begin));
        },
    });

    // str_starts_with : String -> String -> Bool
    list.push_back(BuiltinDef{
        "str_starts_with",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), bool_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& s = std::get<ValString>(args[0].node).value;
            const auto& prefix = std::get<ValString>(args[1].node).value;
            return Value::make_bool(s.size() >= prefix.size() &&
                                    s.compare(0, prefix.size(), prefix) == 0);
        },
    });

    // str_ends_with : String -> String -> Bool
    list.push_back(BuiltinDef{
        "str_ends_with",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), bool_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& s = std::get<ValString>(args[0].node).value;
            const auto& suffix = std::get<ValString>(args[1].node).value;
            return Value::make_bool(s.size() >= suffix.size() &&
                                    s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0);
        },
    });

    // str_upper : String -> String
    list.push_back(BuiltinDef{
        "str_upper",
        TypeScheme{{}, make_tfun(string_type(), string_type())},
        1,
        [](const std::vector<Value>& args) {
            std::string s = std::get<ValString>(args[0].node).value;
            for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return Value::make_string(std::move(s));
        },
    });

    // str_lower : String -> String
    list.push_back(BuiltinDef{
        "str_lower",
        TypeScheme{{}, make_tfun(string_type(), string_type())},
        1,
        [](const std::vector<Value>& args) {
            std::string s = std::get<ValString>(args[0].node).value;
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return Value::make_string(std::move(s));
        },
    });

    // str_replace : String -> String -> String -> String
    list.push_back(BuiltinDef{
        "str_replace",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), make_tfun(string_type(), string_type())))},
        3,
        [](const std::vector<Value>& args) {
            std::string s = std::get<ValString>(args[0].node).value;
            const auto& from = std::get<ValString>(args[1].node).value;
            const auto& to = std::get<ValString>(args[2].node).value;
            if (from.empty()) return Value::make_string(std::move(s));
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
            return Value::make_string(std::move(s));
        },
    });

    // int_of_string : String -> Int
    list.push_back(BuiltinDef{
        "int_of_string",
        TypeScheme{{}, make_tfun(string_type(), int_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& s = std::get<ValString>(args[0].node).value;
            try {
                size_t idx = 0;
                int64_t val = std::stoll(s, &idx, 10);
                if (idx != s.size()) {
                    throw CompileError(Diagnostic{"runtime", "invalid integer string: '" + s + "'", {}});
                }
                return Value::make_int(val);
            } catch (const CompileError&) {
                throw;
            } catch (...) {
                throw CompileError(Diagnostic{"runtime", "invalid integer string: '" + s + "'", {}});
            }
        },
    });

    // float_of_string : String -> Float
    list.push_back(BuiltinDef{
        "float_of_string",
        TypeScheme{{}, make_tfun(string_type(), float_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& s = std::get<ValString>(args[0].node).value;
            try {
                size_t idx = 0;
                double val = std::stod(s, &idx);
                if (idx != s.size()) {
                    throw CompileError(Diagnostic{"runtime", "invalid float string: '" + s + "'", {}});
                }
                return Value::make_float(val);
            } catch (const CompileError&) {
                throw;
            } catch (...) {
                throw CompileError(Diagnostic{"runtime", "invalid float string: '" + s + "'", {}});
            }
        },
    });

    // is_valid_int : String -> Bool
    list.push_back(BuiltinDef{
        "is_valid_int",
        TypeScheme{{}, make_tfun(string_type(), bool_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& s = std::get<ValString>(args[0].node).value;
            if (s.empty()) return Value::make_bool(false);
            try {
                size_t idx = 0;
                (void)std::stoll(s, &idx, 10);
                return Value::make_bool(idx == s.size());
            } catch (...) {
                return Value::make_bool(false);
            }
        },
    });

    // is_valid_float : String -> Bool
    list.push_back(BuiltinDef{
        "is_valid_float",
        TypeScheme{{}, make_tfun(string_type(), bool_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& s = std::get<ValString>(args[0].node).value;
            if (s.empty()) return Value::make_bool(false);
            try {
                size_t idx = 0;
                (void)std::stod(s, &idx);
                return Value::make_bool(idx == s.size());
            } catch (...) {
                return Value::make_bool(false);
            }
        },
    });

    // list_nth : List 'a -> Int -> 'a
    list.push_back(BuiltinDef{
        "list_nth",
        TypeScheme{{0}, make_tfun(list_type(tv(0)), make_tfun(int_type(), tv(0)))},
        2,
        [](const std::vector<Value>& args) {
            const auto& elems = std::get<ValList>(args[0].node).ptr->elems;
            int64_t idx = std::get<ValInt>(args[1].node).value;
            if (idx < 0 || static_cast<size_t>(idx) >= elems.size()) {
                throw CompileError(Diagnostic{"runtime", "list_nth index out of bounds", {}});
            }
            return elems[static_cast<size_t>(idx)];
        },
    });

    // list_rev : List 'a -> List 'a
    list.push_back(BuiltinDef{
        "list_rev",
        TypeScheme{{0}, make_tfun(list_type(tv(0)), list_type(tv(0)))},
        1,
        [](const std::vector<Value>& args) {
            auto elems = std::get<ValList>(args[0].node).ptr->elems;
            std::reverse(elems.begin(), elems.end());
            return Value::make_list(std::move(elems));
        },
    });

    // list_append : List 'a -> List 'a -> List 'a
    list.push_back(BuiltinDef{
        "list_append",
        TypeScheme{{0}, make_tfun(list_type(tv(0)), make_tfun(list_type(tv(0)), list_type(tv(0))))},
        2,
        [](const std::vector<Value>& args) {
            const auto& a = std::get<ValList>(args[0].node).ptr->elems;
            const auto& b = std::get<ValList>(args[1].node).ptr->elems;
            std::vector<Value> out;
            out.reserve(a.size() + b.size());
            out.insert(out.end(), a.begin(), a.end());
            out.insert(out.end(), b.begin(), b.end());
            return Value::make_list(std::move(out));
        },
    });

    // list_slice : List 'a -> Int -> Int -> List 'a
    list.push_back(BuiltinDef{
        "list_slice",
        TypeScheme{{0}, make_tfun(list_type(tv(0)), make_tfun(int_type(), make_tfun(int_type(), list_type(tv(0)))))},
        3,
        [](const std::vector<Value>& args) {
            const auto& elems = std::get<ValList>(args[0].node).ptr->elems;
            int64_t start = std::get<ValInt>(args[1].node).value;
            int64_t len = std::get<ValInt>(args[2].node).value;
            if (start < 0 || len < 0 || static_cast<size_t>(start) > elems.size()) {
                throw CompileError(Diagnostic{"runtime", "list_slice index out of bounds", {}});
            }
            size_t s_idx = static_cast<size_t>(start);
            size_t end_idx = std::min(elems.size(), s_idx + static_cast<size_t>(len));
            return Value::make_list(std::vector<Value>(elems.begin() + s_idx, elems.begin() + end_idx));
        },
    });

    // list_snoc : List 'a -> 'a -> List 'a
    list.push_back(BuiltinDef{
        "list_snoc",
        TypeScheme{{0}, make_tfun(list_type(tv(0)), make_tfun(tv(0), list_type(tv(0))))},
        2,
        [](const std::vector<Value>& args) {
            auto elems = std::get<ValList>(args[0].node).ptr->elems;
            elems.push_back(args[1]);
            return Value::make_list(std::move(elems));
        },
    });
}

void append_io_builtins(std::vector<BuiltinDef>& list) {
    auto tv = [](uint32_t id) { return make_tvar(id); };

    // read_line : Unit -> String
    list.push_back(BuiltinDef{
        "read_line",
        TypeScheme{{}, make_tfun(unit_type(), string_type())},
        1,
        [](const std::vector<Value>&) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                return Value::make_string("");
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return Value::make_string(std::move(line));
        },
    });

    // eprintln : 'a -> Unit
    list.push_back(BuiltinDef{
        "eprintln",
        TypeScheme{{0}, make_tfun(tv(0), unit_type())},
        1,
        [](const std::vector<Value>& args) {
            std::cerr << value_to_string(args[0]) << "\n";
            return Value::unit();
        },
    });

    // cli_args : Unit -> List String
    list.push_back(BuiltinDef{
        "cli_args",
        TypeScheme{{}, make_tfun(unit_type(), list_type(string_type()))},
        1,
        [](const std::vector<Value>&) {
            std::vector<Value> out;
            for (const auto& a : get_cli_args()) {
                out.push_back(Value::make_string(a));
            }
            return Value::make_list(std::move(out));
        },
    });

    // env_get : String -> String
    list.push_back(BuiltinDef{
        "env_get",
        TypeScheme{{}, make_tfun(string_type(), string_type())},
        1,
        [](const std::vector<Value>& args) {
            const char* val = std::getenv(std::get<ValString>(args[0].node).value.c_str());
            return Value::make_string(val ? val : "");
        },
    });

    // exit_with : Int -> 'a
    list.push_back(BuiltinDef{
        "exit_with",
        TypeScheme{{0}, make_tfun(int_type(), tv(0))},
        1,
        [](const std::vector<Value>& args) -> Value {
            throw ProgramExit(static_cast<int>(std::get<ValInt>(args[0].node).value));
        },
    });

    // time_ms : Unit -> Int
    list.push_back(BuiltinDef{
        "time_ms",
        TypeScheme{{}, make_tfun(unit_type(), int_type())},
        1,
        [](const std::vector<Value>&) {
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return Value::make_int(static_cast<int64_t>(ms));
        },
    });

    // sleep_ms : Int -> Unit
    list.push_back(BuiltinDef{
        "sleep_ms",
        TypeScheme{{}, make_tfun(int_type(), unit_type())},
        1,
        [](const std::vector<Value>& args) {
            int64_t ms = std::get<ValInt>(args[0].node).value;
            if (ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
            return Value::unit();
        },
    });

    // read_file : String -> String
    list.push_back(BuiltinDef{
        "read_file",
        TypeScheme{{}, make_tfun(string_type(), string_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs) {
                throw CompileError(Diagnostic{"runtime", "could not open file for reading: '" + path + "'", {}});
            }
            std::ostringstream ss;
            ss << ifs.rdbuf();
            return Value::make_string(ss.str());
        },
    });

    // write_file : String -> String -> Unit
    list.push_back(BuiltinDef{
        "write_file",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), unit_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            const auto& content = std::get<ValString>(args[1].node).value;
            std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                throw CompileError(Diagnostic{"runtime", "could not open file for writing: '" + path + "'", {}});
            }
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            return Value::unit();
        },
    });

    // append_file : String -> String -> Unit
    list.push_back(BuiltinDef{
        "append_file",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), unit_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            const auto& content = std::get<ValString>(args[1].node).value;
            std::ofstream ofs(path, std::ios::binary | std::ios::app);
            if (!ofs) {
                throw CompileError(Diagnostic{"runtime", "could not open file for appending: '" + path + "'", {}});
            }
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            return Value::unit();
        },
    });

    // file_exists : String -> Bool
    list.push_back(BuiltinDef{
        "file_exists",
        TypeScheme{{}, make_tfun(string_type(), bool_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            std::error_code ec;
            return Value::make_bool(std::filesystem::exists(path, ec));
        },
    });

    // delete_file : String -> Bool
    list.push_back(BuiltinDef{
        "delete_file",
        TypeScheme{{}, make_tfun(string_type(), bool_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            std::error_code ec;
            return Value::make_bool(std::filesystem::remove(path, ec));
        },
    });

    // make_dir : String -> Unit
    list.push_back(BuiltinDef{
        "make_dir",
        TypeScheme{{}, make_tfun(string_type(), unit_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            if (ec) {
                throw CompileError(Diagnostic{"runtime", "make_dir failed: " + ec.message(), {}});
            }
            return Value::unit();
        },
    });

    // is_dir : String -> Bool
    list.push_back(BuiltinDef{
        "is_dir",
        TypeScheme{{}, make_tfun(string_type(), bool_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            std::error_code ec;
            return Value::make_bool(std::filesystem::is_directory(path, ec));
        },
    });

    // list_dir : String -> List String
    list.push_back(BuiltinDef{
        "list_dir",
        TypeScheme{{}, make_tfun(string_type(), list_type(string_type()))},
        1,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            std::error_code ec;
            std::vector<std::string> names;
            for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
                names.push_back(entry.path().filename().string());
            }
            if (ec) {
                throw CompileError(Diagnostic{"runtime", "list_dir failed: " + ec.message(), {}});
            }
            std::sort(names.begin(), names.end());
            std::vector<Value> out;
            out.reserve(names.size());
            for (auto& nm : names) out.push_back(Value::make_string(std::move(nm)));
            return Value::make_list(std::move(out));
        },
    });

    // file_size : String -> Int
    list.push_back(BuiltinDef{
        "file_size",
        TypeScheme{{}, make_tfun(string_type(), int_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            std::error_code ec;
            auto sz = std::filesystem::file_size(path, ec);
            if (ec) {
                throw CompileError(Diagnostic{"runtime", "file_size failed: " + ec.message(), {}});
            }
            return Value::make_int(static_cast<int64_t>(sz));
        },
    });

    // copy_file : String -> String -> Unit
    list.push_back(BuiltinDef{
        "copy_file",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), unit_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& src = std::get<ValString>(args[0].node).value;
            const auto& dst = std::get<ValString>(args[1].node).value;
            std::error_code ec;
            std::filesystem::copy_file(
                src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                throw CompileError(Diagnostic{"runtime", "copy_file failed: " + ec.message(), {}});
            }
            return Value::unit();
        },
    });

    // rename_file : String -> String -> Unit
    list.push_back(BuiltinDef{
        "rename_file",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), unit_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& src = std::get<ValString>(args[0].node).value;
            const auto& dst = std::get<ValString>(args[1].node).value;
            std::error_code ec;
            std::filesystem::rename(src, dst, ec);
            if (ec) {
                throw CompileError(Diagnostic{"runtime", "rename_file failed: " + ec.message(), {}});
            }
            return Value::unit();
        },
    });

    struct OpenFileTable {
        std::mutex mu;
        int64_t next_id = 1;
        std::unordered_map<int64_t, std::unique_ptr<std::fstream>> handles;
    };
    static OpenFileTable open_files;

    // file_open : String -> String -> Int
    list.push_back(BuiltinDef{
        "file_open",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(string_type(), int_type()))},
        2,
        [](const std::vector<Value>& args) {
            const auto& path = std::get<ValString>(args[0].node).value;
            const auto& mode = std::get<ValString>(args[1].node).value;
            std::ios::openmode om = std::ios::binary;
            if (mode == "r") om |= std::ios::in;
            else if (mode == "w") om |= std::ios::out | std::ios::trunc;
            else if (mode == "a") om |= std::ios::out | std::ios::app;
            else if (mode == "rw") om |= std::ios::in | std::ios::out;
            else {
                throw CompileError(Diagnostic{"runtime", "file_open invalid mode '" + mode + "'", {}});
            }
            auto fs_ptr = std::make_unique<std::fstream>(path, om);
            if (!fs_ptr->is_open()) {
                throw CompileError(Diagnostic{"runtime", "file_open failed for '" + path + "'", {}});
            }
            std::lock_guard<std::mutex> lk(open_files.mu);
            int64_t id = open_files.next_id++;
            open_files.handles[id] = std::move(fs_ptr);
            return Value::make_int(id);
        },
    });

    // file_read_line : Int -> String
    list.push_back(BuiltinDef{
        "file_read_line",
        TypeScheme{{}, make_tfun(int_type(), string_type())},
        1,
        [](const std::vector<Value>& args) {
            int64_t id = std::get<ValInt>(args[0].node).value;
            std::lock_guard<std::mutex> lk(open_files.mu);
            auto it = open_files.handles.find(id);
            if (it == open_files.handles.end()) {
                throw CompileError(Diagnostic{"runtime", "invalid file handle", {}});
            }
            std::string line;
            if (!std::getline(*it->second, line)) return Value::make_string("");
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return Value::make_string(std::move(line));
        },
    });

    // file_read_all : Int -> String
    list.push_back(BuiltinDef{
        "file_read_all",
        TypeScheme{{}, make_tfun(int_type(), string_type())},
        1,
        [](const std::vector<Value>& args) {
            int64_t id = std::get<ValInt>(args[0].node).value;
            std::lock_guard<std::mutex> lk(open_files.mu);
            auto it = open_files.handles.find(id);
            if (it == open_files.handles.end()) {
                throw CompileError(Diagnostic{"runtime", "invalid file handle", {}});
            }
            std::ostringstream ss;
            ss << it->second->rdbuf();
            return Value::make_string(ss.str());
        },
    });

    // file_write : Int -> String -> Unit
    list.push_back(BuiltinDef{
        "file_write",
        TypeScheme{{}, make_tfun(int_type(), make_tfun(string_type(), unit_type()))},
        2,
        [](const std::vector<Value>& args) {
            int64_t id = std::get<ValInt>(args[0].node).value;
            const auto& content = std::get<ValString>(args[1].node).value;
            std::lock_guard<std::mutex> lk(open_files.mu);
            auto it = open_files.handles.find(id);
            if (it == open_files.handles.end()) {
                throw CompileError(Diagnostic{"runtime", "invalid file handle", {}});
            }
            it->second->write(content.data(), static_cast<std::streamsize>(content.size()));
            return Value::unit();
        },
    });

    // file_close : Int -> Unit
    list.push_back(BuiltinDef{
        "file_close",
        TypeScheme{{}, make_tfun(int_type(), unit_type())},
        1,
        [](const std::vector<Value>& args) {
            int64_t id = std::get<ValInt>(args[0].node).value;
            std::lock_guard<std::mutex> lk(open_files.mu);
            open_files.handles.erase(id);
            return Value::unit();
        },
    });

    // sys_exec : String -> Int
    list.push_back(BuiltinDef{
        "sys_exec",
        TypeScheme{{}, make_tfun(string_type(), int_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& cmd = std::get<ValString>(args[0].node).value;
            int rc = std::system(cmd.c_str());
            return Value::make_int(static_cast<int64_t>(rc));
        },
    });

    // vm_exec_bytecode : String -> String
    list.push_back(BuiltinDef{
        "vm_exec_bytecode",
        TypeScheme{{}, make_tfun(string_type(), string_type())},
        1,
        [](const std::vector<Value>& args) {
            const auto& bc_src = std::get<ValString>(args[0].node).value;
            BytecodeProgram bp = deserialize_bytecode(bc_src);
            VM vm;
            for (const auto& c : bp.ctors) {
                if (c.arity == 0) {
                    vm.set_global(c.name, Value::make_adt(c.tag, c.name, {}));
                } else {
                    vm.set_global(c.name, Value{ValCtor{c.tag, c.name, c.arity, {}}});
                }
            }
            Value res = vm.run(bp.entry);
            return Value::make_string(value_to_string(res));
        },
    });
}

}  // namespace cml::detail
