#pragma once

#include "cml/source.hpp"
#include "cml/token.hpp"

#include <vector>

namespace cml {

class Lexer {
public:
    explicit Lexer(const SourceFile& source);

    std::vector<Token> tokenize();

private:
    char peek(size_t offset = 0) const;
    bool at_end() const;
    char advance();
    void skip_whitespace_and_comments();
    Token scan_token();
    Token scan_number(size_t start_offset, size_t start_line, size_t start_col);
    Token scan_string(size_t start_offset, size_t start_line, size_t start_col);
    Token scan_ident(size_t start_offset, size_t start_line, size_t start_col);

    [[noreturn]] void error(const std::string& msg, size_t offset, size_t line, size_t col,
                            size_t len = 1) const;

    const SourceFile& source_;
    const std::string& text_;
    size_t pos_ = 0;
    size_t line_ = 1;
    size_t col_ = 1;
};

}  // namespace cml
