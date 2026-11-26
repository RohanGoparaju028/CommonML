#include "cml/highlight.hpp"
#include "cml/lexer.hpp"
#include "cml/source.hpp"

#include <cctype>
#include <sstream>
#include <vector>

namespace cml {

namespace {

const char* ansi_for_token(TokenKind kind) {
    switch (kind) {
        case TokenKind::KwLet:
        case TokenKind::KwVar:
        case TokenKind::KwRec:
        case TokenKind::KwIn:
        case TokenKind::KwFn:
        case TokenKind::KwIf:
        case TokenKind::KwThen:
        case TokenKind::KwElse:
        case TokenKind::KwMatch:
        case TokenKind::KwWith:
        case TokenKind::KwType:
        case TokenKind::KwStruct:
        case TokenKind::KwMacro:
        case TokenKind::KwQuote:
        case TokenKind::KwImport:
        case TokenKind::KwNew:
        case TokenKind::KwDefer:
        case TokenKind::KwClose:
        case TokenKind::KwWhile:
        case TokenKind::KwFor:
        case TokenKind::KwEach:
        case TokenKind::KwBreak:
        case TokenKind::KwContinue:
        case TokenKind::KwBegin:
        case TokenKind::KwEnd:
            return "\033[1;35m";
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
            return "\033[36m";
        case TokenKind::Int:
        case TokenKind::Float:
            return "\033[33m";
        case TokenKind::String:
            return "\033[32m";
        case TokenKind::UpperIdent:
            return "\033[1;34m";
        case TokenKind::Dollar:
        case TokenKind::Bang:
            return "\033[1;33m";
        case TokenKind::PipeGt:
        case TokenKind::ColonColon:
        case TokenKind::PlusPlus:
        case TokenKind::Arrow:
        case TokenKind::Eq:
        case TokenKind::EqEq:
        case TokenKind::BangEq:
        case TokenKind::Lt:
        case TokenKind::LtEq:
        case TokenKind::Gt:
        case TokenKind::GtEq:
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
        case TokenKind::PlusDot:
        case TokenKind::MinusDot:
        case TokenKind::StarDot:
        case TokenKind::SlashDot:
        case TokenKind::AmpAmp:
        case TokenKind::PipePipe:
        case TokenKind::Pipe:
        case TokenKind::Amp:
        case TokenKind::Caret:
        case TokenKind::Tilde:
        case TokenKind::LtLt:
        case TokenKind::GtGt:
            return "\033[36m";
        default:
            return "";
    }
}

void emit_gap(std::ostringstream& os, const std::string& gap) {
    size_t i = 0;
    while (i < gap.size()) {
        if (std::isspace(static_cast<unsigned char>(gap[i]))) {
            size_t start = i;
            while (i < gap.size() && std::isspace(static_cast<unsigned char>(gap[i]))) ++i;
            os << gap.substr(start, i - start);
        } else {
            size_t start = i;
            while (i < gap.size() && !std::isspace(static_cast<unsigned char>(gap[i]))) ++i;
            os << "\033[2;37m" << gap.substr(start, i - start) << "\033[0m";
        }
    }
}

}  // namespace

std::string highlight_source(const std::string& filename, const std::string& code, bool use_ansi) {
    if (!use_ansi) return code;

    std::vector<Token> tokens;
    try {
        SourceFile src(filename, code);
        Lexer lexer(src);
        tokens = lexer.tokenize();
    } catch (const CompileError&) {
        return code;
    }

    std::ostringstream os;
    size_t cursor = 0;
    for (const auto& tok : tokens) {
        if (tok.kind == TokenKind::Eof) break;
        if (tok.kind == TokenKind::Newline || tok.kind == TokenKind::Indent ||
            tok.kind == TokenKind::Dedent) {
            continue;
        }
        size_t start = tok.span.offset;
        size_t len = tok.span.length;
        if (start > code.size() || start < cursor) continue;
        if (start > cursor) {
            emit_gap(os, code.substr(cursor, start - cursor));
        }
        size_t end = std::min(code.size(), start + len);
        const char* color = ansi_for_token(tok.kind);
        if (*color) {
            os << color << code.substr(start, end - start) << "\033[0m";
        } else {
            os << code.substr(start, end - start);
        }
        cursor = end;
    }
    if (cursor < code.size()) {
        emit_gap(os, code.substr(cursor));
    }
    return os.str();
}

}  // namespace cml
