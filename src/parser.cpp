#include "cml/parser.hpp"

#include <utility>

namespace cml {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek(size_t offset) const {
    if (pos_ + offset >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[pos_ + offset];
}

const Token& Parser::previous() const {
    return tokens_[pos_ > 0 ? pos_ - 1 : 0];
}

bool Parser::at_end() const {
    return peek().kind == TokenKind::Eof;
}

bool Parser::check(TokenKind kind) const {
    return peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::advance() {
    if (!at_end()) {
        ++pos_;
    }
    return previous();
}

const Token& Parser::expect(TokenKind kind, const std::string& msg) {
    if (check(kind)) {
        return advance();
    }
    error(peek(), msg + " (got " + token_kind_name(peek().kind) + ")");
}

void Parser::error(const Token& tok, const std::string& msg) const {
    throw CompileError(Diagnostic{"parse", msg, tok.span});
}

Program Parser::parse_program() {
    Program prog;
    while (match(TokenKind::Semicolon)) {}
    while (!at_end()) {
        prog.stmts.push_back(parse_stmt());
        while (match(TokenKind::Semicolon)) {}
    }
    return prog;
}

ExprPtr Parser::parse_single_expr() {
    auto expr = parse_seq_expr();
    if (!at_end()) {
        error(peek(), "unexpected trailing token");
    }
    return expr;
}

StmtPtr Parser::parse_stmt() {
    size_t prev_col = top_stmt_col_;
    top_stmt_col_ = peek().span.column;
    struct Restore {
        size_t& target;
        size_t old;
        ~Restore() { target = old; }
    } restore{top_stmt_col_, prev_col};

    if (check(TokenKind::KwType)) {
        return parse_type_decl();
    }
    if (check(TokenKind::KwLet)) {
        return parse_let_stmt();
    }
    if (check(TokenKind::KwMacro)) {
        return parse_macro_stmt();
    }
    auto expr = parse_expr();
    return std::make_shared<Stmt>(Stmt{StmtExpr{expr}, expr->span});
}

StmtPtr Parser::parse_type_decl() {
    const Token& kw = expect(TokenKind::KwType, "expected 'type'");
    const Token& name_tok = expect(TokenKind::UpperIdent, "expected type name");
    Ident type_name{name_tok.lexeme, {}, name_tok.span};

    std::vector<Ident> type_params;
    while (check(TokenKind::LowerIdent)) {
        const Token& tp = advance();
        type_params.push_back(Ident{tp.lexeme, {}, tp.span});
    }

    expect(TokenKind::Eq, "expected '=' in type declaration");
    match(TokenKind::Pipe);

    std::vector<CtorDecl> ctors;
    auto parse_ctor = [&]() {
        const Token& cname = expect(TokenKind::UpperIdent, "expected constructor name");
        std::vector<TypeExprPtr> fields;
        while (check(TokenKind::LowerIdent) || check(TokenKind::UpperIdent) ||
               check(TokenKind::LParen)) {
            fields.push_back(parse_type_atom());
        }
        SourceSpan cspan = cname.span;
        if (!fields.empty()) {
            cspan = SourceSpan::merge(cspan, fields.back()->span);
        }
        ctors.push_back(CtorDecl{Ident{cname.lexeme, {}, cname.span}, std::move(fields), cspan});
    };

    parse_ctor();
    while (match(TokenKind::Pipe)) {
        parse_ctor();
    }

    SourceSpan span = SourceSpan::merge(kw.span, ctors.back().span);
    return std::make_shared<Stmt>(
        Stmt{StmtTypeDecl{std::move(type_name), std::move(type_params), std::move(ctors)}, span});
}

Parser::Param Parser::parse_param() {
    if (check(TokenKind::LParen) && peek(1).kind != TokenKind::RParen) {
        size_t saved = pos_;
        advance();
        auto pat = parse_pattern();
        if (match(TokenKind::Colon)) {
            auto ty = parse_type();
            const Token& rp = expect(TokenKind::RParen, "expected ')' after parameter type");
            SourceSpan span = SourceSpan::merge(tokens_[saved].span, rp.span);
            return Param{std::move(pat), std::move(ty), span};
        }
        pos_ = saved;
    }
    auto pat = parse_pat_atom();
    return Param{pat, std::nullopt, pat->span};
}

static ExprPtr wrap_fn_params(const std::vector<Parser::Param>& params,
                              const std::optional<TypeExprPtr>& ret_ann,
                              ExprPtr body) {
    if (ret_ann) {
        body = std::make_shared<Expr>(
            Expr{ExprAnnot{body, *ret_ann}, SourceSpan::merge(body->span, (*ret_ann)->span)});
    }
    for (auto it = params.rbegin(); it != params.rend(); ++it) {
        SourceSpan span = SourceSpan::merge(it->span, body->span);
        body = std::make_shared<Expr>(Expr{ExprFn{it->pat, it->type, body}, span});
    }
    return body;
}

StmtPtr Parser::parse_let_stmt() {
    const Token& kw = expect(TokenKind::KwLet, "expected 'let'");
    bool is_rec = match(TokenKind::KwRec);

    PatternPtr pat;
    std::vector<Param> params;

    if (check(TokenKind::LowerIdent) && peek(1).kind != TokenKind::Eq &&
        peek(1).kind != TokenKind::Colon) {
        const Token& fn_tok = advance();
        pat = std::make_shared<Pattern>(
            Pattern{PatVar{Ident{fn_tok.lexeme, {}, fn_tok.span}}, fn_tok.span});
        while (!check(TokenKind::Eq) && !check(TokenKind::Colon) && !at_end()) {
            params.push_back(parse_param());
        }
    } else {
        pat = parse_pattern();
    }

    std::optional<TypeExprPtr> ann;
    if (match(TokenKind::Colon)) {
        ann = parse_type();
    }

    expect(TokenKind::Eq, "expected '=' in let binding");
    ExprPtr val = parse_expr();

    if (!params.empty()) {
        val = wrap_fn_params(params, ann, val);
        ann = std::nullopt;
    }

    if (is_rec && !std::holds_alternative<PatVar>(pat->node)) {
        error(peek(), "'let rec' requires a simple variable name");
    }

    SourceSpan span = SourceSpan::merge(kw.span, val->span);
    return std::make_shared<Stmt>(
        Stmt{StmtLet{is_rec, std::move(pat), std::move(ann), std::move(val)}, span});
}

StmtPtr Parser::parse_macro_stmt() {
    const Token& kw = expect(TokenKind::KwMacro, "expected 'macro'");
    const Token& name_tok = expect(TokenKind::LowerIdent, "expected macro name");
    Ident name{name_tok.lexeme, {}, name_tok.span};

    std::vector<Ident> params;
    while (check(TokenKind::LowerIdent)) {
        const Token& p = advance();
        params.push_back(Ident{p.lexeme, {}, p.span});
    }
    expect(TokenKind::Eq, "expected '=' in macro definition");
    ExprPtr body = parse_expr();
    SourceSpan span = SourceSpan::merge(kw.span, body->span);
    return std::make_shared<Stmt>(
        Stmt{StmtMacro{std::move(name), std::move(params), std::move(body)}, span});
}

TypeExprPtr Parser::parse_type() {
    auto left = parse_type_tuple();
    if (match(TokenKind::Arrow)) {
        auto right = parse_type();
        SourceSpan span = SourceSpan::merge(left->span, right->span);
        return std::make_shared<TypeExpr>(TypeExpr{TFun{left, right}, span});
    }
    return left;
}

TypeExprPtr Parser::parse_type_tuple() {
    auto first = parse_type_app();
    if (!check(TokenKind::Star)) {
        return first;
    }
    std::vector<TypeExprPtr> elems{first};
    while (match(TokenKind::Star)) {
        elems.push_back(parse_type_app());
    }
    SourceSpan span = SourceSpan::merge(elems.front()->span, elems.back()->span);
    return std::make_shared<TypeExpr>(TypeExpr{TTuple{std::move(elems)}, span});
}

TypeExprPtr Parser::parse_type_app() {
    if (check(TokenKind::UpperIdent)) {
        const Token& con = advance();
        std::vector<TypeExprPtr> args;
        while (check(TokenKind::LowerIdent) || check(TokenKind::UpperIdent) ||
               check(TokenKind::LParen)) {
            args.push_back(parse_type_atom());
        }
        SourceSpan span = con.span;
        if (!args.empty()) {
            span = SourceSpan::merge(span, args.back()->span);
        }
        return std::make_shared<TypeExpr>(TypeExpr{TCon{con.lexeme, std::move(args)}, span});
    }
    return parse_type_atom();
}

TypeExprPtr Parser::parse_type_atom() {
    if (check(TokenKind::LowerIdent)) {
        const Token& tok = advance();
        return std::make_shared<TypeExpr>(TypeExpr{TVar{tok.lexeme}, tok.span});
    }
    if (check(TokenKind::UpperIdent)) {
        const Token& tok = advance();
        return std::make_shared<TypeExpr>(TypeExpr{TCon{tok.lexeme, {}}, tok.span});
    }
    if (match(TokenKind::LParen)) {
        auto ty = parse_type();
        expect(TokenKind::RParen, "expected ')' in type expression");
        return ty;
    }
    error(peek(), "expected type expression");
}

bool Parser::starts_pat_atom() const {
    switch (peek().kind) {
        case TokenKind::Underscore:
        case TokenKind::LowerIdent:
        case TokenKind::UpperIdent:
        case TokenKind::Int:
        case TokenKind::Float:
        case TokenKind::String:
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
        case TokenKind::LParen:
        case TokenKind::LBracket:
        case TokenKind::Dollar:
            return true;
        default:
            return false;
    }
}

PatternPtr Parser::parse_pattern() {
    return parse_pat_cons();
}

PatternPtr Parser::parse_pat_cons() {
    auto head = parse_pat_app();
    if (match(TokenKind::ColonColon)) {
        auto tail = parse_pat_cons();
        SourceSpan span = SourceSpan::merge(head->span, tail->span);
        return std::make_shared<Pattern>(Pattern{PatCons{head, tail}, span});
    }
    return head;
}

PatternPtr Parser::parse_pat_app() {
    if (check(TokenKind::UpperIdent)) {
        const Token& ctor = advance();
        std::vector<PatternPtr> args;
        while (starts_pat_atom()) {
            args.push_back(parse_pat_atom());
        }
        SourceSpan span = ctor.span;
        if (!args.empty()) {
            span = SourceSpan::merge(span, args.back()->span);
        }
        return std::make_shared<Pattern>(
            Pattern{PatCtor{Ident{ctor.lexeme, {}, ctor.span}, std::move(args)}, span});
    }
    return parse_pat_atom();
}

PatternPtr Parser::parse_pat_atom() {
    const Token& tok = peek();
    if (match(TokenKind::Underscore)) {
        return std::make_shared<Pattern>(Pattern{PatWildcard{}, tok.span});
    }
    if (match(TokenKind::LowerIdent)) {
        return std::make_shared<Pattern>(
            Pattern{PatVar{Ident{tok.lexeme, {}, tok.span}}, tok.span});
    }
    if (match(TokenKind::UpperIdent)) {
        return std::make_shared<Pattern>(
            Pattern{PatCtor{Ident{tok.lexeme, {}, tok.span}, {}}, tok.span});
    }
    if (match(TokenKind::Int)) {
        return std::make_shared<Pattern>(Pattern{PatInt{tok.int_val}, tok.span});
    }
    if (match(TokenKind::Float)) {
        return std::make_shared<Pattern>(Pattern{PatFloat{tok.float_val}, tok.span});
    }
    if (match(TokenKind::String)) {
        return std::make_shared<Pattern>(Pattern{PatString{tok.str_val}, tok.span});
    }
    if (match(TokenKind::KwTrue)) {
        return std::make_shared<Pattern>(Pattern{PatBool{true}, tok.span});
    }
    if (match(TokenKind::KwFalse)) {
        return std::make_shared<Pattern>(Pattern{PatBool{false}, tok.span});
    }
    if (match(TokenKind::Dollar)) {
        if (quote_depth_ <= 0) {
            error(tok, "'$' unquote can only be used inside 'quote { ... }'");
        }
        ExprPtr e;
        if (match(TokenKind::LParen)) {
            --quote_depth_;
            e = parse_seq_expr();
            ++quote_depth_;
            const Token& rp = expect(TokenKind::RParen, "expected ')' after unquote");
            return std::make_shared<Pattern>(
                Pattern{PatUnquote{e}, SourceSpan::merge(tok.span, rp.span)});
        }
        const Token& id = expect(TokenKind::LowerIdent, "expected identifier or '(' after '$'");
        e = std::make_shared<Expr>(Expr{ExprVar{Ident{id.lexeme, {}, id.span}}, id.span});
        return std::make_shared<Pattern>(
            Pattern{PatUnquote{e}, SourceSpan::merge(tok.span, id.span)});
    }
    if (match(TokenKind::LParen)) {
        if (match(TokenKind::RParen)) {
            return std::make_shared<Pattern>(
                Pattern{PatUnit{}, SourceSpan::merge(tok.span, previous().span)});
        }
        auto first = parse_pattern();
        if (match(TokenKind::Comma)) {
            std::vector<PatternPtr> elems{first};
            do {
                elems.push_back(parse_pattern());
            } while (match(TokenKind::Comma));
            const Token& rp = expect(TokenKind::RParen, "expected ')' in tuple pattern");
            return std::make_shared<Pattern>(
                Pattern{PatTuple{std::move(elems)}, SourceSpan::merge(tok.span, rp.span)});
        }
        expect(TokenKind::RParen, "expected ')' in pattern");
        return first;
    }
    if (match(TokenKind::LBracket)) {
        std::vector<PatternPtr> elems;
        if (!check(TokenKind::RBracket)) {
            do {
                elems.push_back(parse_pattern());
            } while (match(TokenKind::Comma));
        }
        const Token& rb = expect(TokenKind::RBracket, "expected ']' in list pattern");
        return std::make_shared<Pattern>(
            Pattern{PatList{std::move(elems)}, SourceSpan::merge(tok.span, rb.span)});
    }
    error(tok, "expected pattern");
}

ExprPtr Parser::parse_expr() {
    if (check(TokenKind::KwLet)) return parse_let_expr();
    if (check(TokenKind::KwFn)) return parse_fn_expr();
    if (check(TokenKind::KwIf)) return parse_if_expr();
    if (check(TokenKind::KwMatch)) return parse_match_expr();
    return parse_binary(1);
}

ExprPtr Parser::parse_seq_expr() {
    auto first = parse_expr();
    while (match(TokenKind::Semicolon)) {
        if (check(TokenKind::RParen) || check(TokenKind::RBrace) || check(TokenKind::KwEnd)) {
            break;
        }
        auto next = parse_expr();
        SourceSpan span = SourceSpan::merge(first->span, next->span);
        first = std::make_shared<Expr>(Expr{ExprBinary{BinaryOp::Seq, first, next}, span});
    }
    return first;
}

ExprPtr Parser::parse_let_expr() {
    const Token& kw = expect(TokenKind::KwLet, "expected 'let'");
    bool is_rec = match(TokenKind::KwRec);

    PatternPtr pat;
    std::vector<Param> params;

    if (check(TokenKind::LowerIdent) && peek(1).kind != TokenKind::Eq &&
        peek(1).kind != TokenKind::Colon) {
        const Token& fn_tok = advance();
        pat = std::make_shared<Pattern>(
            Pattern{PatVar{Ident{fn_tok.lexeme, {}, fn_tok.span}}, fn_tok.span});
        while (!check(TokenKind::Eq) && !check(TokenKind::Colon) && !at_end()) {
            params.push_back(parse_param());
        }
    } else {
        pat = parse_pattern();
    }

    std::optional<TypeExprPtr> ann;
    if (match(TokenKind::Colon)) {
        ann = parse_type();
    }
    expect(TokenKind::Eq, "expected '=' in let expression");
    ExprPtr val = parse_expr();

    if (!params.empty()) {
        val = wrap_fn_params(params, ann, val);
        ann = std::nullopt;
    }

    if (is_rec && !std::holds_alternative<PatVar>(pat->node)) {
        error(peek(), "'let rec' requires a simple variable name");
    }

    expect(TokenKind::KwIn, "expected 'in' after let binding");
    ExprPtr body = parse_expr();
    SourceSpan span = SourceSpan::merge(kw.span, body->span);
    return std::make_shared<Expr>(
        Expr{ExprLet{is_rec, std::move(pat), std::move(ann), std::move(val), std::move(body)},
             span});
}

ExprPtr Parser::parse_fn_expr() {
    const Token& kw = expect(TokenKind::KwFn, "expected 'fn'");
    std::vector<Param> params;
    do {
        params.push_back(parse_param());
    } while (!check(TokenKind::Arrow) && !at_end());
    expect(TokenKind::Arrow, "expected '->' in fn expression");
    ExprPtr body = parse_expr();
    ExprPtr fn = wrap_fn_params(params, std::nullopt, body);
    return std::make_shared<Expr>(Expr{fn->node, SourceSpan::merge(kw.span, body->span)});
}

ExprPtr Parser::parse_if_expr() {
    const Token& kw = expect(TokenKind::KwIf, "expected 'if'");
    ExprPtr cond = parse_expr();
    expect(TokenKind::KwThen, "expected 'then'");
    ExprPtr then_br = parse_expr();
    expect(TokenKind::KwElse, "expected 'else'");
    ExprPtr else_br = parse_expr();
    SourceSpan span = SourceSpan::merge(kw.span, else_br->span);
    return std::make_shared<Expr>(Expr{ExprIf{cond, then_br, else_br}, span});
}

ExprPtr Parser::parse_match_expr() {
    const Token& kw = expect(TokenKind::KwMatch, "expected 'match'");
    ExprPtr scrutinee = parse_expr();
    expect(TokenKind::KwWith, "expected 'with'");
    match(TokenKind::Pipe);

    std::vector<MatchArm> arms;
    auto parse_arm = [&]() {
        PatternPtr pat = parse_pattern();
        expect(TokenKind::Arrow, "expected '->' in match arm");
        ExprPtr body = parse_expr();
        arms.push_back(MatchArm{pat, body, SourceSpan::merge(pat->span, body->span)});
    };
    parse_arm();
    while (match(TokenKind::Pipe)) {
        parse_arm();
    }
    SourceSpan span = SourceSpan::merge(kw.span, arms.back().span);
    return std::make_shared<Expr>(Expr{ExprMatch{scrutinee, std::move(arms)}, span});
}

struct BinOpInfo {
    BinaryOp op;
    int prec;
    bool right_assoc;
};

static std::optional<BinOpInfo> lookup_binop(TokenKind k) {
    switch (k) {
        case TokenKind::PipeGt: return BinOpInfo{BinaryOp::Pipe, 1, false};
        case TokenKind::PipePipe: return BinOpInfo{BinaryOp::Or, 2, false};
        case TokenKind::AmpAmp: return BinOpInfo{BinaryOp::And, 3, false};
        case TokenKind::EqEq: return BinOpInfo{BinaryOp::Eq, 4, false};
        case TokenKind::BangEq: return BinOpInfo{BinaryOp::Neq, 4, false};
        case TokenKind::Lt: return BinOpInfo{BinaryOp::Lt, 4, false};
        case TokenKind::LtEq: return BinOpInfo{BinaryOp::Le, 4, false};
        case TokenKind::Gt: return BinOpInfo{BinaryOp::Gt, 4, false};
        case TokenKind::GtEq: return BinOpInfo{BinaryOp::Ge, 4, false};
        case TokenKind::ColonColon: return BinOpInfo{BinaryOp::Cons, 5, true};
        case TokenKind::Plus: return BinOpInfo{BinaryOp::AddInt, 6, false};
        case TokenKind::Minus: return BinOpInfo{BinaryOp::SubInt, 6, false};
        case TokenKind::PlusDot: return BinOpInfo{BinaryOp::AddFloat, 6, false};
        case TokenKind::MinusDot: return BinOpInfo{BinaryOp::SubFloat, 6, false};
        case TokenKind::PlusPlus: return BinOpInfo{BinaryOp::ConcatStr, 6, false};
        case TokenKind::Star: return BinOpInfo{BinaryOp::MulInt, 7, false};
        case TokenKind::Slash: return BinOpInfo{BinaryOp::DivInt, 7, false};
        case TokenKind::Percent: return BinOpInfo{BinaryOp::ModInt, 7, false};
        case TokenKind::StarDot: return BinOpInfo{BinaryOp::MulFloat, 7, false};
        case TokenKind::SlashDot: return BinOpInfo{BinaryOp::DivFloat, 7, false};
        default: return std::nullopt;
    }
}

ExprPtr Parser::parse_binary(int min_prec) {
    auto left = parse_unary();
    while (true) {
        auto info = lookup_binop(peek().kind);
        if (!info || info->prec < min_prec) {
            break;
        }
        advance();
        int next_min = info->right_assoc ? info->prec : info->prec + 1;
        ExprPtr right = (info->op == BinaryOp::Pipe &&
                         (check(TokenKind::KwFn) || check(TokenKind::KwIf) ||
                          check(TokenKind::KwLet) || check(TokenKind::KwMatch)))
                            ? parse_expr()
                            : parse_binary(next_min);
        SourceSpan span = SourceSpan::merge(left->span, right->span);
        left = std::make_shared<Expr>(Expr{ExprBinary{info->op, left, right}, span});
    }
    return left;
}

ExprPtr Parser::parse_unary() {
    if (check(TokenKind::Minus) || check(TokenKind::MinusDot)) {
        const Token& op_tok = advance();
        UnaryOp op = (op_tok.kind == TokenKind::MinusDot) ? UnaryOp::NegFloat : UnaryOp::NegInt;
        auto operand = parse_unary();
        SourceSpan span = SourceSpan::merge(op_tok.span, operand->span);
        return std::make_shared<Expr>(Expr{ExprUnary{op, operand}, span});
    }
    return parse_app();
}

bool Parser::starts_atom() const {
    switch (peek().kind) {
        case TokenKind::Int:
        case TokenKind::Float:
        case TokenKind::String:
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
        case TokenKind::LowerIdent:
        case TokenKind::UpperIdent:
        case TokenKind::LParen:
        case TokenKind::LBracket:
        case TokenKind::KwQuote:
        case TokenKind::KwBegin:
        case TokenKind::Dollar:
            return true;
        default:
            return false;
    }
}

ExprPtr Parser::parse_app() {
    auto fn = parse_atom();
    while (starts_atom()) {
        if (expr_delim_depth_ == 0 && top_stmt_col_ > 0 &&
            peek().span.line > previous().span.line && peek().span.column <= top_stmt_col_) {
            break;
        }
        auto arg = parse_atom();
        SourceSpan span = SourceSpan::merge(fn->span, arg->span);
        fn = std::make_shared<Expr>(Expr{ExprApp{fn, arg}, span});
    }
    return fn;
}

ExprPtr Parser::parse_atom() {
    const Token& tok = peek();
    if (match(TokenKind::Int)) {
        return std::make_shared<Expr>(Expr{ExprInt{tok.int_val}, tok.span});
    }
    if (match(TokenKind::Float)) {
        return std::make_shared<Expr>(Expr{ExprFloat{tok.float_val}, tok.span});
    }
    if (match(TokenKind::String)) {
        return std::make_shared<Expr>(Expr{ExprString{tok.str_val}, tok.span});
    }
    if (match(TokenKind::KwTrue)) {
        return std::make_shared<Expr>(Expr{ExprBool{true}, tok.span});
    }
    if (match(TokenKind::KwFalse)) {
        return std::make_shared<Expr>(Expr{ExprBool{false}, tok.span});
    }
    if (match(TokenKind::LowerIdent)) {
        Ident id{tok.lexeme, {}, tok.span};
        if (match(TokenKind::Bang)) {
            expect(TokenKind::LParen, "expected '(' after '!' in macro call");
            std::vector<ExprPtr> args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parse_expr());
                } while (match(TokenKind::Comma));
            }
            const Token& rp = expect(TokenKind::RParen, "expected ')' after macro arguments");
            return std::make_shared<Expr>(
                Expr{ExprMacroCall{std::move(id), std::move(args)},
                     SourceSpan::merge(tok.span, rp.span)});
        }
        return std::make_shared<Expr>(Expr{ExprVar{std::move(id)}, tok.span});
    }
    if (match(TokenKind::UpperIdent)) {
        return std::make_shared<Expr>(
            Expr{ExprCtor{Ident{tok.lexeme, {}, tok.span}}, tok.span});
    }
    if (match(TokenKind::KwQuote)) {
        expect(TokenKind::LBrace, "expected '{' after 'quote'");
        ++quote_depth_;
        ++expr_delim_depth_;
        ExprPtr body = parse_seq_expr();
        --expr_delim_depth_;
        --quote_depth_;
        const Token& rb = expect(TokenKind::RBrace, "expected '}' after quoted expression");
        return std::make_shared<Expr>(
            Expr{ExprQuote{body}, SourceSpan::merge(tok.span, rb.span)});
    }
    if (match(TokenKind::Dollar)) {
        if (quote_depth_ <= 0) {
            error(tok, "'$' unquote can only be used inside 'quote { ... }'");
        }
        if (match(TokenKind::LParen)) {
            --quote_depth_;
            ++expr_delim_depth_;
            ExprPtr e = parse_seq_expr();
            --expr_delim_depth_;
            ++quote_depth_;
            const Token& rp = expect(TokenKind::RParen, "expected ')' after unquote expression");
            return std::make_shared<Expr>(
                Expr{ExprUnquote{e}, SourceSpan::merge(tok.span, rp.span)});
        }
        const Token& id = expect(TokenKind::LowerIdent, "expected identifier or '(' after '$'");
        auto var = std::make_shared<Expr>(
            Expr{ExprVar{Ident{id.lexeme, {}, id.span}}, id.span});
        return std::make_shared<Expr>(
            Expr{ExprUnquote{var}, SourceSpan::merge(tok.span, id.span)});
    }
    if (match(TokenKind::KwBegin)) {
        ++expr_delim_depth_;
        ExprPtr body = parse_seq_expr();
        --expr_delim_depth_;
        const Token& end_tok = expect(TokenKind::KwEnd, "expected 'end' after 'begin'");
        return std::make_shared<Expr>(
            Expr{body->node, SourceSpan::merge(tok.span, end_tok.span)});
    }
    if (match(TokenKind::LParen)) {
        if (match(TokenKind::RParen)) {
            return std::make_shared<Expr>(
                Expr{ExprUnit{}, SourceSpan::merge(tok.span, previous().span)});
        }
        ++expr_delim_depth_;
        ExprPtr first = parse_expr();
        if (match(TokenKind::Colon)) {
            TypeExprPtr ty = parse_type();
            --expr_delim_depth_;
            const Token& rp = expect(TokenKind::RParen, "expected ')' after type annotation");
            return std::make_shared<Expr>(
                Expr{ExprAnnot{first, ty}, SourceSpan::merge(tok.span, rp.span)});
        }
        if (match(TokenKind::Comma)) {
            std::vector<ExprPtr> elems{first};
            do {
                elems.push_back(parse_expr());
            } while (match(TokenKind::Comma));
            --expr_delim_depth_;
            const Token& rp = expect(TokenKind::RParen, "expected ')' in tuple");
            return std::make_shared<Expr>(
                Expr{ExprTuple{std::move(elems)}, SourceSpan::merge(tok.span, rp.span)});
        }
        if (match(TokenKind::Semicolon)) {
            ExprPtr acc = first;
            while (!check(TokenKind::RParen) && !at_end()) {
                ExprPtr next = parse_expr();
                acc = std::make_shared<Expr>(
                    Expr{ExprBinary{BinaryOp::Seq, acc, next},
                         SourceSpan::merge(acc->span, next->span)});
                if (!match(TokenKind::Semicolon)) {
                    break;
                }
            }
            --expr_delim_depth_;
            const Token& rp = expect(TokenKind::RParen, "expected ')' after sequence");
            return std::make_shared<Expr>(
                Expr{acc->node, SourceSpan::merge(tok.span, rp.span)});
        }
        --expr_delim_depth_;
        expect(TokenKind::RParen, "expected ')'");
        return first;
    }
    if (match(TokenKind::LBracket)) {
        std::vector<ExprPtr> elems;
        ++expr_delim_depth_;
        if (!check(TokenKind::RBracket)) {
            do {
                elems.push_back(parse_expr());
            } while (match(TokenKind::Comma));
        }
        --expr_delim_depth_;
        const Token& rb = expect(TokenKind::RBracket, "expected ']' in list literal");
        return std::make_shared<Expr>(
            Expr{ExprList{std::move(elems)}, SourceSpan::merge(tok.span, rb.span)});
    }
    error(tok, "expected expression");
}

}  // namespace cml
