#pragma once

#include "cml/source.hpp"

#include <cstdint>
#include <string>

namespace cml {

enum class TokenKind {
    Eof,

    Int,
    Float,
    String,
    LowerIdent,
    UpperIdent,

    KwLet,
    KwRec,
    KwIn,
    KwFn,
    KwIf,
    KwThen,
    KwElse,
    KwMatch,
    KwWith,
    KwType,
    KwMacro,
    KwQuote,
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
    Underscore,

    Eq,
    EqEq,
    BangEq,
    Lt,
    LtEq,
    Gt,
    GtEq,
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
