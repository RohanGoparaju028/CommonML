#pragma once

#include "cml/builtins.hpp"

#include <vector>

namespace cml::detail {

void append_string_list_builtins(std::vector<BuiltinDef>& list);
void append_collection_builtins(std::vector<BuiltinDef>& list);
void append_io_builtins(std::vector<BuiltinDef>& list);
void append_net_builtins(std::vector<BuiltinDef>& list);

}  // namespace cml::detail
