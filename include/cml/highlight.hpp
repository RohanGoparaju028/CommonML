#pragma once

#include <string>

namespace cml {

std::string highlight_source(const std::string& filename, const std::string& code,
                             bool use_ansi = true);

}  // namespace cml
