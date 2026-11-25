#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace cml {

struct SourceSpan {
    std::string filename = "<input>";
    size_t line = 1;
    size_t column = 1;
    size_t length = 1;
    size_t offset = 0;

    static SourceSpan merge(const SourceSpan& a, const SourceSpan& b);
};

class SourceFile {
public:
    SourceFile() = default;
    SourceFile(std::string filename, std::string content);

    const std::string& filename() const { return filename_; }
    const std::string& content() const { return content_; }
    std::string line_text(size_t line_1based) const;

private:
    std::string filename_ = "<input>";
    std::string content_;
    std::vector<size_t> line_starts_{0};
};

struct Diagnostic {
    std::string stage;
    std::string message;
    SourceSpan span;

    std::string format(const SourceFile* source = nullptr) const;
};

class CompileError : public std::runtime_error {
public:
    explicit CompileError(Diagnostic diag)
        : std::runtime_error(diag.message), diag_(std::move(diag)) {}

    const Diagnostic& diagnostic() const { return diag_; }

private:
    Diagnostic diag_;
};

}  // namespace cml
