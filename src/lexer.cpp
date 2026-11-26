#include "cml/lexer.hpp"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace cml {

const char* token_kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::Eof: return "end of file";
        case TokenKind::Newline: return "newline";
        case TokenKind::Indent: return "indent";
        case TokenKind::Dedent: return "dedent";
        case TokenKind::Int: return "integer";
        case TokenKind::Float: return "float";
        case TokenKind::String: return "string";
        case TokenKind::LowerIdent: return "identifier";
        case TokenKind::UpperIdent: return "constructor/type name";
        case TokenKind::KwLet: return "'let'";
        case TokenKind::KwVar: return "'var'";
        case TokenKind::KwRec: return "'rec'";
        case TokenKind::KwIn: return "'in'";
        case TokenKind::KwFn: return "'fn'";
        case TokenKind::KwIf: return "'if'";
        case TokenKind::KwThen: return "'then'";
        case TokenKind::KwElse: return "'else'";
        case TokenKind::KwMatch: return "'match'";
        case TokenKind::KwWith: return "'with'";
        case TokenKind::KwType: return "'type'";
        case TokenKind::KwStruct: return "'struct'";
        case TokenKind::KwMacro: return "'macro'";
        case TokenKind::KwQuote: return "'quote'";
        case TokenKind::KwImport: return "'import'";
        case TokenKind::KwNew: return "'new'";
        case TokenKind::KwDefer: return "'defer'";
        case TokenKind::KwClose: return "'close'";
        case TokenKind::KwWhile: return "'while'";
        case TokenKind::KwFor: return "'for'";
        case TokenKind::KwEach: return "'each'";
        case TokenKind::KwBreak: return "'break'";
        case TokenKind::KwContinue: return "'continue'";
        case TokenKind::KwTrue: return "'true'";
        case TokenKind::KwFalse: return "'false'";
        case TokenKind::KwBegin: return "'begin'";
        case TokenKind::KwEnd: return "'end'";
        case TokenKind::LParen: return "'('";
        case TokenKind::RParen: return "')'";
        case TokenKind::LBracket: return "'['";
        case TokenKind::RBracket: return "']'";
        case TokenKind::LBrace: return "'{'";
        case TokenKind::RBrace: return "'}'";
        case TokenKind::Comma: return "','";
        case TokenKind::Colon: return "':'";
        case TokenKind::ColonColon: return "'::'";
        case TokenKind::Semicolon: return "';'";
        case TokenKind::Pipe: return "'|'";
        case TokenKind::PipeGt: return "'|>'";
        case TokenKind::Arrow: return "'->'";
        case TokenKind::Dot: return "'.'";
        case TokenKind::Dollar: return "'$'";
        case TokenKind::Bang: return "'!'";
        case TokenKind::Amp: return "'&'";
        case TokenKind::Caret: return "'^'";
        case TokenKind::Tilde: return "'~'";
        case TokenKind::Underscore: return "'_'";
        case TokenKind::Eq: return "'='";
        case TokenKind::EqEq: return "'=='";
        case TokenKind::BangEq: return "'!='";
        case TokenKind::Lt: return "'<'";
        case TokenKind::LtEq: return "'<='";
        case TokenKind::Gt: return "'>'";
        case TokenKind::GtEq: return "'>='";
        case TokenKind::LtLt: return "'<<'";
        case TokenKind::GtGt: return "'>>'";
        case TokenKind::Plus: return "'+'";
        case TokenKind::Minus: return "'-'";
        case TokenKind::Star: return "'*'";
        case TokenKind::Slash: return "'/'";
        case TokenKind::Percent: return "'%'";
        case TokenKind::PlusDot: return "'+.'";
        case TokenKind::MinusDot: return "'-.'";
        case TokenKind::StarDot: return "'*.'";
        case TokenKind::SlashDot: return "'/.'";
        case TokenKind::PlusPlus: return "'++'";
        case TokenKind::AmpAmp: return "'&&'";
        case TokenKind::PipePipe: return "'||'";
    }
    return "unknown";
}

Lexer::Lexer(const SourceFile& source)
    : source_(source), text_(source.content()) {}

char Lexer::peek(size_t offset) const {
    if (pos_ + offset >= text_.size()) {
        return '\0';
    }
    return text_[pos_ + offset];
}

bool Lexer::at_end() const {
    return pos_ >= text_.size();
}

char Lexer::advance() {
    char c = text_[pos_++];
    if (c == '\n') {
        ++line_;
        col_ = 1;
    } else {
        ++col_;
    }
    return c;
}

void Lexer::error(const std::string& msg, size_t offset, size_t line, size_t col,
                  size_t len) const {
    throw CompileError(Diagnostic{"lex", msg,
                                  SourceSpan{source_.filename(), line, col, len, offset}});
}

void Lexer::skip_horizontal_and_comments() {
    while (!at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
            continue;
        }
        if ((c == '-' && peek(1) == '-') || (c == '/' && peek(1) == '/')) {
            while (!at_end() && peek() != '\n') {
                advance();
            }
            continue;
        }
        if (c == '(' && peek(1) == '*') {
            size_t start_off = pos_, start_line = line_, start_col = col_;
            advance();
            advance();
            int depth = 1;
            while (!at_end() && depth > 0) {
                if (peek() == '(' && peek(1) == '*') {
                    advance();
                    advance();
                    ++depth;
                } else if (peek() == '*' && peek(1) == ')') {
                    advance();
                    advance();
                    --depth;
                } else {
                    advance();
                }
            }
            if (depth > 0) {
                error("unterminated '(*' block comment", start_off, start_line, start_col, 2);
            }
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            size_t start_off = pos_, start_line = line_, start_col = col_;
            advance();
            advance();
            while (!at_end() && !(peek() == '*' && peek(1) == '/')) {
                advance();
            }
            if (at_end()) {
                error("unterminated '/*' block comment", start_off, start_line, start_col, 2);
            }
            advance();
            advance();
            continue;
        }
        break;
    }
}

void Lexer::skip_whitespace_and_comments() {
    while (!at_end()) {
        skip_horizontal_and_comments();
        if (peek() == '\n') {
            advance();
            continue;
        }
        break;
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    paren_depth_ = 0;
    at_line_start_ = true;
    indent_stack_.clear();

    while (true) {
        if (paren_depth_ > 0) {
            skip_whitespace_and_comments();
            if (at_end()) {
                while (indent_stack_.size() > 1) {
                    indent_stack_.pop_back();
                    tokens.push_back(Token{TokenKind::Dedent, "", 0, 0.0, "",
                                           SourceSpan{source_.filename(), line_, col_, 1, pos_}});
                }
                tokens.push_back(Token{TokenKind::Eof, "", 0, 0.0, "",
                                       SourceSpan{source_.filename(), line_, col_, 1, pos_}});
                break;
            }
        } else if (at_line_start_) {
            size_t line_indent = 0;
            while (true) {
                line_indent = 0;
                while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
                    char c = advance();
                    if (c == ' ') ++line_indent;
                    else if (c == '\t') line_indent += 4;
                }
                skip_horizontal_and_comments();
                if (!at_end() && peek() == '\n') {
                    advance();
                    continue;
                }
                break;
            }

            if (at_end()) {
                while (indent_stack_.size() > 1) {
                    indent_stack_.pop_back();
                    tokens.push_back(Token{TokenKind::Dedent, "", 0, 0.0, "",
                                           SourceSpan{source_.filename(), line_, col_, 1, pos_}});
                }
                tokens.push_back(Token{TokenKind::Eof, "", 0, 0.0, "",
                                       SourceSpan{source_.filename(), line_, col_, 1, pos_}});
                break;
            }

            if (indent_stack_.empty()) {
                indent_stack_.push_back(line_indent);
            } else if (line_indent > indent_stack_.back()) {
                indent_stack_.push_back(line_indent);
                tokens.push_back(Token{TokenKind::Indent, "", 0, 0.0, "",
                                       SourceSpan{source_.filename(), line_, col_, 1, pos_}});
            } else if (line_indent == indent_stack_.back()) {
                if (!tokens.empty() && tokens.back().kind != TokenKind::Newline &&
                    tokens.back().kind != TokenKind::Indent) {
                    tokens.push_back(Token{TokenKind::Newline, "\n", 0, 0.0, "",
                                           SourceSpan{source_.filename(), line_, col_, 1, pos_}});
                }
            } else {
                while (indent_stack_.size() > 1 && line_indent < indent_stack_.back()) {
                    indent_stack_.pop_back();
                    tokens.push_back(Token{TokenKind::Dedent, "", 0, 0.0, "",
                                           SourceSpan{source_.filename(), line_, col_, 1, pos_}});
                }
                if (line_indent != indent_stack_.back()) {
                    error("inconsistent indentation", pos_, line_, col_, 1);
                }
            }
            at_line_start_ = false;
        } else {
            skip_horizontal_and_comments();
            if (at_end()) {
                while (indent_stack_.size() > 1) {
                    indent_stack_.pop_back();
                    tokens.push_back(Token{TokenKind::Dedent, "", 0, 0.0, "",
                                           SourceSpan{source_.filename(), line_, col_, 1, pos_}});
                }
                tokens.push_back(Token{TokenKind::Eof, "", 0, 0.0, "",
                                       SourceSpan{source_.filename(), line_, col_, 1, pos_}});
                break;
            }
            if (peek() == '\n') {
                advance();
                at_line_start_ = true;
                continue;
            }
        }

        Token tok = scan_token();
        if (tok.kind == TokenKind::LParen || tok.kind == TokenKind::LBracket ||
            tok.kind == TokenKind::LBrace) {
            ++paren_depth_;
        } else if (tok.kind == TokenKind::RParen || tok.kind == TokenKind::RBracket ||
                   tok.kind == TokenKind::RBrace) {
            if (paren_depth_ > 0) --paren_depth_;
        }
        tokens.push_back(std::move(tok));
    }
    return tokens;
}

Token Lexer::scan_number(size_t start_offset, size_t start_line, size_t start_col) {
    std::string raw;
    bool is_float = false;
    while (!at_end() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
        char c = advance();
        if (c != '_') {
            raw.push_back(c);
        }
    }
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        is_float = true;
        raw.push_back(advance());
        while (!at_end() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
            char c = advance();
            if (c != '_') {
                raw.push_back(c);
            }
        }
    }
    if (peek() == 'e' || peek() == 'E') {
        is_float = true;
        raw.push_back(advance());
        if (peek() == '+' || peek() == '-') {
            raw.push_back(advance());
        }
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            error("malformed exponent in float literal", start_offset, start_line, start_col,
                  pos_ - start_offset);
        }
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            raw.push_back(advance());
        }
    }

    Token tok;
    tok.lexeme = text_.substr(start_offset, pos_ - start_offset);
    tok.span = SourceSpan{source_.filename(), start_line, start_col, pos_ - start_offset, start_offset};
    if (is_float) {
        tok.kind = TokenKind::Float;
        tok.float_val = std::strtod(raw.c_str(), nullptr);
    } else {
        tok.kind = TokenKind::Int;
        try {
            tok.int_val = std::stoll(raw);
        } catch (...) {
            error("integer literal out of 64-bit range", start_offset, start_line, start_col,
                  pos_ - start_offset);
        }
    }
    return tok;
}

Token Lexer::scan_string(size_t start_offset, size_t start_line, size_t start_col) {
    advance();
    std::string value;
    while (!at_end() && peek() != '"') {
        char c = advance();
        if (c == '\\') {
            if (at_end()) {
                error("unterminated escape sequence in string", start_offset, start_line, start_col,
                      pos_ - start_offset);
            }
            char esc = advance();
            switch (esc) {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case 'r': value.push_back('\r'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                case '0': value.push_back('\0'); break;
                default:
                    error(std::string("unknown escape sequence '\\") + esc + "'",
                          pos_ - 2, line_, col_ > 2 ? col_ - 2 : 1, 2);
            }
        } else {
            value.push_back(c);
        }
    }
    if (at_end()) {
        error("unterminated string literal", start_offset, start_line, start_col,
              pos_ - start_offset);
    }
    advance();
    Token tok;
    tok.kind = TokenKind::String;
    tok.lexeme = text_.substr(start_offset, pos_ - start_offset);
    tok.str_val = std::move(value);
    tok.span = SourceSpan{source_.filename(), start_line, start_col, pos_ - start_offset, start_offset};
    return tok;
}

Token Lexer::scan_ident(size_t start_offset, size_t start_line, size_t start_col) {
    while (!at_end()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '\'') {
            advance();
        } else {
            break;
        }
    }
    std::string word = text_.substr(start_offset, pos_ - start_offset);
    SourceSpan span{source_.filename(), start_line, start_col, word.size(), start_offset};

    if (word == "_") {
        return Token{TokenKind::Underscore, word, 0, 0.0, "", span};
    }

    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"let", TokenKind::KwLet},
        {"var", TokenKind::KwVar},
        {"rec", TokenKind::KwRec},
        {"in", TokenKind::KwIn},
        {"fn", TokenKind::KwFn},
        {"if", TokenKind::KwIf},
        {"then", TokenKind::KwThen},
        {"else", TokenKind::KwElse},
        {"match", TokenKind::KwMatch},
        {"with", TokenKind::KwWith},
        {"type", TokenKind::KwType},
        {"struct", TokenKind::KwStruct},
        {"macro", TokenKind::KwMacro},
        {"quote", TokenKind::KwQuote},
        {"import", TokenKind::KwImport},
        {"new", TokenKind::KwNew},
        {"defer", TokenKind::KwDefer},
        {"close", TokenKind::KwClose},
        {"while", TokenKind::KwWhile},
        {"for", TokenKind::KwFor},
        {"each", TokenKind::KwEach},
        {"break", TokenKind::KwBreak},
        {"continue", TokenKind::KwContinue},
        {"true", TokenKind::KwTrue},
        {"false", TokenKind::KwFalse},
        {"begin", TokenKind::KwBegin},
        {"end", TokenKind::KwEnd},
    };
    auto it = keywords.find(word);
    if (it != keywords.end()) {
        return Token{it->second, word, 0, 0.0, "", span};
    }
    bool is_upper = std::isupper(static_cast<unsigned char>(word[0])) != 0;
    return Token{is_upper ? TokenKind::UpperIdent : TokenKind::LowerIdent,
                 word, 0, 0.0, "", span};
}

Token Lexer::scan_token() {
    size_t start_off = pos_;
    size_t start_line = line_;
    size_t start_col = col_;
    char c = peek();

    if (std::isdigit(static_cast<unsigned char>(c))) {
        return scan_number(start_off, start_line, start_col);
    }
    if (c == '"') {
        return scan_string(start_off, start_line, start_col);
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return scan_ident(start_off, start_line, start_col);
    }

    advance();
    auto make = [&](TokenKind kind, size_t len = 1) {
        return Token{kind, text_.substr(start_off, len), 0, 0.0, "",
                     SourceSpan{source_.filename(), start_line, start_col, len, start_off}};
    };

    switch (c) {
        case '(': return make(TokenKind::LParen);
        case ')': return make(TokenKind::RParen);
        case '[': return make(TokenKind::LBracket);
        case ']': return make(TokenKind::RBracket);
        case '{': return make(TokenKind::LBrace);
        case '}': return make(TokenKind::RBrace);
        case ',': return make(TokenKind::Comma);
        case ';': return make(TokenKind::Semicolon);
        case '.': return make(TokenKind::Dot);
        case '$': return make(TokenKind::Dollar);
        case '%': return make(TokenKind::Percent);
        case '^': return make(TokenKind::Caret);
        case '~': return make(TokenKind::Tilde);
        case ':':
            if (peek() == ':') { advance(); return make(TokenKind::ColonColon, 2); }
            return make(TokenKind::Colon);
        case '|':
            if (peek() == '>') { advance(); return make(TokenKind::PipeGt, 2); }
            if (peek() == '|') { advance(); return make(TokenKind::PipePipe, 2); }
            return make(TokenKind::Pipe);
        case '&':
            if (peek() == '&') { advance(); return make(TokenKind::AmpAmp, 2); }
            return make(TokenKind::Amp);
        case '=':
            if (peek() == '=') { advance(); return make(TokenKind::EqEq, 2); }
            return make(TokenKind::Eq);
        case '!':
            if (peek() == '=') { advance(); return make(TokenKind::BangEq, 2); }
            return make(TokenKind::Bang);
        case '<':
            if (peek() == '<') { advance(); return make(TokenKind::LtLt, 2); }
            if (peek() == '=') { advance(); return make(TokenKind::LtEq, 2); }
            return make(TokenKind::Lt);
        case '>':
            if (peek() == '>') { advance(); return make(TokenKind::GtGt, 2); }
            if (peek() == '=') { advance(); return make(TokenKind::GtEq, 2); }
            return make(TokenKind::Gt);
        case '+':
            if (peek() == '+') { advance(); return make(TokenKind::PlusPlus, 2); }
            if (peek() == '.') { advance(); return make(TokenKind::PlusDot, 2); }
            return make(TokenKind::Plus);
        case '-':
            if (peek() == '>') { advance(); return make(TokenKind::Arrow, 2); }
            if (peek() == '.') { advance(); return make(TokenKind::MinusDot, 2); }
            return make(TokenKind::Minus);
        case '*':
            if (peek() == '.') { advance(); return make(TokenKind::StarDot, 2); }
            return make(TokenKind::Star);
        case '/':
            if (peek() == '.') { advance(); return make(TokenKind::SlashDot, 2); }
            return make(TokenKind::Slash);
        default:
            error(std::string("unexpected character '") + c + "'", start_off, start_line, start_col);
    }
}

}  // namespace cml
