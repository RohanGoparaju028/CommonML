#include "cml/source.hpp"

#include <algorithm>
#include <sstream>

namespace cml {

SourceSpan SourceSpan::merge(const SourceSpan& a, const SourceSpan& b) {
    if (a.filename != b.filename) {
        return a;
    }
    if (b.offset < a.offset) {
        return merge(b, a);
    }
    size_t end_a = a.offset + a.length;
    size_t end_b = b.offset + b.length;
    size_t end = std::max(end_a, end_b);
    SourceSpan out = a;
    out.length = std::max<size_t>(1, end - a.offset);
    return out;
}

SourceFile::SourceFile(std::string filename, std::string content)
    : filename_(std::move(filename)), content_(std::move(content)) {
    line_starts_.clear();
    line_starts_.push_back(0);
    for (size_t i = 0; i < content_.size(); ++i) {
        if (content_[i] == '\n') {
            line_starts_.push_back(i + 1);
        }
    }
}

std::string SourceFile::line_text(size_t line_1based) const {
    if (line_1based == 0 || line_1based > line_starts_.size()) {
        return "";
    }
    size_t start = line_starts_[line_1based - 1];
    size_t end = (line_1based < line_starts_.size())
                     ? line_starts_[line_1based]
                     : content_.size();
    while (end > start && (content_[end - 1] == '\n' || content_[end - 1] == '\r')) {
        --end;
    }
    return content_.substr(start, end - start);
}

std::string Diagnostic::format(const SourceFile* source) const {
    std::ostringstream out;
    out << span.filename << ":" << span.line << ":" << span.column
        << ": " << stage << " error: " << message << "\n";
    if (source != nullptr && source->filename() == span.filename) {
        std::string line = source->line_text(span.line);
        if (!line.empty()) {
            std::string lnum = std::to_string(span.line);
            std::string pad(lnum.size(), ' ');
            out << " " << pad << " |\n";
            out << " " << lnum << " | " << line << "\n";
            out << " " << pad << " | ";
            size_t col_pad = span.column > 0 ? span.column - 1 : 0;
            for (size_t i = 0; i < col_pad; ++i) {
                out << (i < line.size() && line[i] == '\t' ? '\t' : ' ');
            }
            size_t carets = std::max<size_t>(1, span.length);
            for (size_t i = 0; i < carets; ++i) {
                out << '^';
            }
            out << "\n";
        }
    }
    return out.str();
}

}  // namespace cml
