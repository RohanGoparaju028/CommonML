#pragma once

#include "cml/source.hpp"

#include <cstdint>
#include <string>

namespace cml {

enum class TokenKind {
    Eof,
    Newline,
    Indent,
    Dedent,

    Int,
    Float,
    String,
    LowerIdent,
    UpperIdent,

    KwLet,
    KwVar,
    KwRec,
    KwIn,
    KwFn,
    KwIf,
    KwThen,
    KwElse,
    KwMatch,
    KwWith,
    KwType,
    KwStruct,
    KwMacro,
    KwQuote,
    KwImport,
    KwNew,
    KwDefer,
    KwClose,
    KwWhile,
    KwFor,
    KwEach,
    KwBreak,
    KwContinue,
    KwTrue,
    KwFalse,
    KwBegin,
    KwEnd,

    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Comma,
    Colon,
    ColonColon,
    Semicolon,
    Pipe,
    PipeGt,
    Arrow,
    Dot,
    Dollar,
    Bang,
    Amp,
    Caret,
    Tilde,
    Underscore,

    Eq,
    EqEq,
    BangEq,
    Lt,
    LtEq,
    Gt,
    GtEq,
    LtLt,
    GtGt,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    PlusDot,
    MinusDot,
    StarDot,
    SlashDot,
    PlusPlus,
    AmpAmp,
    PipePipe,
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string lexeme;
    int64_t int_val = 0;
    double float_val = 0.0;
    std::string str_val;
    SourceSpan span;
};

const char* token_kind_name(TokenKind kind);

}  // namespace cml
