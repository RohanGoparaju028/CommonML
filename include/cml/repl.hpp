#pragma once

#include "cml/pipeline.hpp"

#include <istream>
#include <ostream>

namespace cml {

class Repl {
public:
    Repl() = default;

    int run(std::istream& in, std::ostream& out, std::ostream& err);

private:
    bool handle_command(const std::string& line, std::ostream& out, std::ostream& err,
                        bool& should_quit);

    Pipeline pipeline_;
    EvalOptions opts_;
    size_t eval_counter_ = 1;
};

}  // namespace cml
