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

void Parser::skip_stmt_seps() {
    while (match(TokenKind::Newline) || match(TokenKind::Semicolon)) {}
}

bool Parser::starts_indented_block() const {
    return check(TokenKind::Indent) ||
           (check(TokenKind::Newline) && peek(1).kind == TokenKind::Indent);
}

Program Parser::parse_program() {
    Program prog;
    skip_stmt_seps();
    while (!at_end()) {
        prog.stmts.push_back(parse_stmt(false));
        skip_stmt_seps();
    }
    return prog;
}

ExprPtr Parser::parse_single_expr() {
    skip_stmt_seps();
    auto expr = parse_seq_expr();
    skip_stmt_seps();
    if (!at_end()) {
        error(peek(), "unexpected trailing token");
    }
    return expr;
}

StmtPtr Parser::parse_stmt(bool in_block) {
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
    if (check(TokenKind::KwStruct)) {
        return parse_struct_decl();
    }
    if (check(TokenKind::KwLet)) {
        return parse_let_stmt(in_block);
    }
    if (check(TokenKind::KwVar)) {
        return parse_var_stmt();
    }
    if (check(TokenKind::KwMacro)) {
        return parse_macro_stmt();
    }
    return parse_assign_or_expr();
}

StmtPtr Parser::parse_assign_or_expr() {
    auto expr = parse_expr();
    if (match(TokenKind::Eq)) {
        auto rhs = parse_block_or_expr();
        SourceSpan span = SourceSpan::merge(expr->span, rhs->span);
        if (auto* v = std::get_if<ExprVar>(&expr->node)) {
            auto assign = std::make_shared<Expr>(Expr{ExprAssign{v->ident, rhs}, span});
            return std::make_shared<Stmt>(Stmt{StmtExpr{assign}, span});
        }
        if (auto* fg = std::get_if<ExprFieldGet>(&expr->node)) {
            auto fset =
                std::make_shared<Expr>(Expr{ExprFieldSet{fg->obj, fg->field, rhs}, span});
            return std::make_shared<Stmt>(Stmt{StmtExpr{fset}, span});
        }
        if (auto* pfg = std::get_if<ExprPtrFieldGet>(&expr->node)) {
            auto pfset =
                std::make_shared<Expr>(Expr{ExprPtrFieldSet{pfg->ptr, pfg->field, rhs}, span});
            return std::make_shared<Stmt>(Stmt{StmtExpr{pfset}, span});
        }
        if (auto* un = std::get_if<ExprUnary>(&expr->node); un && un->op == UnaryOp::Deref) {
            auto dset =
                std::make_shared<Expr>(Expr{ExprDerefSet{un->operand, rhs}, span});
            return std::make_shared<Stmt>(Stmt{StmtExpr{dset}, span});
        }
        error(peek(), "invalid assignment target");
    }
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
    bool has_indent = false;
    if (starts_indented_block()) {
        match(TokenKind::Newline);
        expect(TokenKind::Indent, "expected indent in type declaration");
        has_indent = true;
    } else {
        match(TokenKind::Newline);
    }
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
    while (true) {
        if (match(TokenKind::Pipe)) {
            parse_ctor();
        } else if (check(TokenKind::Newline) && peek(1).kind == TokenKind::Pipe) {
            advance();
            advance();
            parse_ctor();
        } else {
            break;
        }
    }
    if (has_indent) {
        match(TokenKind::Newline);
        expect(TokenKind::Dedent, "expected dedent after type declaration");
    }

    SourceSpan span = SourceSpan::merge(kw.span, ctors.back().span);
    return std::make_shared<Stmt>(
        Stmt{StmtTypeDecl{std::move(type_name), std::move(type_params), std::move(ctors)}, span});
}

StmtPtr Parser::parse_struct_body(const SourceSpan& start_span, const Ident& struct_name,
                                  std::vector<Ident> type_params) {
    std::vector<StructFieldDecl> fields;
    SourceSpan end_span = struct_name.span;

    // Allow optional ':' or '=' before '{' or indented block (e.g. `let rect = struct:`)
    if (check(TokenKind::Colon) || check(TokenKind::Eq)) {
        advance();
    }

    if (match(TokenKind::LBrace)) {
        ++expr_delim_depth_;
        if (!check(TokenKind::RBrace)) {
            while (!check(TokenKind::RBrace) && !at_end()) {
                const Token& fname = expect(TokenKind::LowerIdent, "expected field name");
                expect(TokenKind::Colon, "expected ':' after struct field name");
                TypeExprPtr fty = parse_type();
                SourceSpan fspan = SourceSpan::merge(fname.span, fty->span);
                fields.push_back(
                    StructFieldDecl{Ident{fname.lexeme, {}, fname.span}, std::move(fty), fspan});
                match(TokenKind::Comma);
            }
        }
        --expr_delim_depth_;
        const Token& rb = expect(TokenKind::RBrace, "expected '}' after struct fields");
        end_span = rb.span;
    } else if (starts_indented_block()) {
        match(TokenKind::Newline);
        expect(TokenKind::Indent, "expected indented struct fields");
        while (!check(TokenKind::Dedent) && !at_end()) {
            skip_stmt_seps();
            if (check(TokenKind::Dedent) || at_end()) break;
            const Token& fname = expect(TokenKind::LowerIdent, "expected field name");
            expect(TokenKind::Colon, "expected ':' after struct field name");
            TypeExprPtr fty = parse_type();
            SourceSpan fspan = SourceSpan::merge(fname.span, fty->span);
            end_span = fspan;
            fields.push_back(
                StructFieldDecl{Ident{fname.lexeme, {}, fname.span}, std::move(fty), fspan});
            skip_stmt_seps();
        }
        expect(TokenKind::Dedent, "expected dedent after struct fields");
    } else {
        error(peek(), "expected '{' or indented block in struct declaration");
    }

    SourceSpan span = SourceSpan::merge(start_span, end_span);
    return std::make_shared<Stmt>(
        Stmt{StmtStructDecl{struct_name, std::move(type_params), std::move(fields)}, span});
}

StmtPtr Parser::parse_struct_decl() {
    const Token& kw = expect(TokenKind::KwStruct, "expected 'struct'");
    const Token& name_tok =
        (check(TokenKind::UpperIdent) || check(TokenKind::LowerIdent))
            ? advance()
            : expect(TokenKind::UpperIdent, "expected struct name");
    Ident struct_name{name_tok.lexeme, {}, name_tok.span};

    std::vector<Ident> type_params;
    while (check(TokenKind::LowerIdent)) {
        const Token& tp = advance();
        type_params.push_back(Ident{tp.lexeme, {}, tp.span});
    }

    return parse_struct_body(kw.span, struct_name, std::move(type_params));
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

StmtPtr Parser::parse_let_stmt(bool in_block) {
    const Token& kw = expect(TokenKind::KwLet, "expected 'let'");
    bool is_rec = match(TokenKind::KwRec);

    if (!is_rec && (check(TokenKind::LowerIdent) || check(TokenKind::UpperIdent)) &&
        peek(1).kind == TokenKind::Eq && peek(2).kind == TokenKind::KwStruct) {
        const Token& sname = advance();
        advance();  // '='
        advance();  // 'struct'
        std::vector<Ident> type_params;
        while (check(TokenKind::LowerIdent)) {
            const Token& tp = advance();
            type_params.push_back(Ident{tp.lexeme, {}, tp.span});
        }
        return parse_struct_body(kw.span, Ident{sname.lexeme, {}, sname.span},
                                 std::move(type_params));
    }

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
    ExprPtr val = parse_block_or_expr();

    if (!params.empty()) {
        val = wrap_fn_params(params, ann, val);
        ann = std::nullopt;
    }

    if (is_rec && !std::holds_alternative<PatVar>(pat->node)) {
        error(peek(), "'let rec' requires a simple variable name");
    }

    if (in_block && (check(TokenKind::KwIn) ||
                     (check(TokenKind::Newline) && peek(1).kind == TokenKind::KwIn))) {
        match(TokenKind::Newline);
        advance();
        if (check(TokenKind::Newline) && peek(1).kind != TokenKind::Indent) {
            advance();
        }
        ExprPtr body = parse_block_or_expr();
        SourceSpan span = SourceSpan::merge(kw.span, body->span);
        auto let_e = std::make_shared<Expr>(
            Expr{ExprLet{is_rec, std::move(pat), std::move(ann), std::move(val), std::move(body)},
                 span});
        return std::make_shared<Stmt>(Stmt{StmtExpr{let_e}, span});
    }

    SourceSpan span = SourceSpan::merge(kw.span, val->span);
    return std::make_shared<Stmt>(
        Stmt{StmtLet{is_rec, std::move(pat), std::move(ann), std::move(val)}, span});
}

StmtPtr Parser::parse_var_stmt() {
    const Token& kw = expect(TokenKind::KwVar, "expected 'var'");
    const Token& name_tok = expect(TokenKind::LowerIdent, "expected variable name after 'var'");
    Ident name{name_tok.lexeme, {}, name_tok.span};
    std::optional<TypeExprPtr> ann;
    if (match(TokenKind::Colon)) {
        ann = parse_type();
    }
    expect(TokenKind::Eq, "expected '=' in var declaration");
    ExprPtr val = parse_block_or_expr();
    SourceSpan span = SourceSpan::merge(kw.span, val->span);
    return std::make_shared<Stmt>(
        Stmt{StmtVar{std::move(name), std::move(ann), std::move(val)}, span});
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
    ExprPtr body = parse_block_or_expr();
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
    if (match(TokenKind::Star)) {
        const Token& star_tok = previous();
        auto inner = parse_type_atom();
        SourceSpan span = SourceSpan::merge(star_tok.span, inner->span);
        return std::make_shared<TypeExpr>(TypeExpr{TPtr{std::move(inner)}, span});
    }
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
            error(tok, "'$' unquote pattern can only be used inside 'quote { ... }'");
        }
        if (match(TokenKind::LParen)) {
            --quote_depth_;
            ExprPtr e = parse_seq_expr();
            ++quote_depth_;
            const Token& rp = expect(TokenKind::RParen, "expected ')' after unquote pattern");
            return std::make_shared<Pattern>(
                Pattern{PatUnquote{e}, SourceSpan::merge(tok.span, rp.span)});
        }
        const Token& id = expect(TokenKind::LowerIdent, "expected identifier or '(' after '$'");
        auto var = std::make_shared<Expr>(
            Expr{ExprVar{Ident{id.lexeme, {}, id.span}}, id.span});
        return std::make_shared<Pattern>(
            Pattern{PatUnquote{var}, SourceSpan::merge(tok.span, id.span)});
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
            const Token& rp = expect(TokenKind::RParen, "expected ')' after tuple pattern");
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

ExprPtr Parser::parse_block() {
    match(TokenKind::Newline);
    const Token& ind = expect(TokenKind::Indent, "expected indented block");
    std::vector<StmtPtr> stmts;
    SourceSpan end_span = ind.span;

    while (!check(TokenKind::Dedent) && !at_end()) {
        skip_stmt_seps();
        if (check(TokenKind::Dedent) || at_end()) break;
        StmtPtr s = parse_stmt(true);
        end_span = s->span;
        stmts.push_back(std::move(s));
        skip_stmt_seps();
    }
    const Token& ded = expect(TokenKind::Dedent, "expected dedent after block");
    SourceSpan span = SourceSpan::merge(ind.span, stmts.empty() ? ded.span : end_span);
    return std::make_shared<Expr>(Expr{ExprBlock{std::move(stmts)}, span});
}

ExprPtr Parser::parse_block_or_expr() {
    if (starts_indented_block()) {
        return parse_block();
    }
    return parse_expr();
}

ExprPtr Parser::parse_expr() {
    if (match(TokenKind::KwDefer)) {
        const Token& kw = previous();
        if (check(TokenKind::Colon) || check(TokenKind::Eq)) {
            advance();
        }
        ExprPtr action = parse_block_or_expr();
        SourceSpan span = SourceSpan::merge(kw.span, action->span);
        return std::make_shared<Expr>(Expr{ExprDefer{std::move(action)}, span});
    }
    if (check(TokenKind::KwLet)) {
        return parse_let_expr();
    }
    if (check(TokenKind::KwFn)) {
        return parse_fn_expr();
    }
    if (check(TokenKind::KwIf)) {
        return parse_if_expr();
    }
    if (check(TokenKind::KwMatch)) {
        return parse_match_expr();
    }
    if (check(TokenKind::KwWhile)) {
        return parse_while_expr();
    }
    if (check(TokenKind::KwFor)) {
        return parse_for_expr();
    }
    return parse_binary(1);
}

ExprPtr Parser::parse_seq_expr() {
    auto first = parse_expr();
    while (match(TokenKind::Semicolon)) {
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
    ExprPtr val = parse_block_or_expr();

    if (!params.empty()) {
        val = wrap_fn_params(params, ann, val);
        ann = std::nullopt;
    }

    if (is_rec && !std::holds_alternative<PatVar>(pat->node)) {
        error(peek(), "'let rec' requires a simple variable name");
    }

    if (check(TokenKind::Newline) && peek(1).kind == TokenKind::KwIn) {
        advance();
    }
    expect(TokenKind::KwIn, "expected 'in' after let binding");
    if (check(TokenKind::Newline) && peek(1).kind != TokenKind::Indent) {
        advance();
    }
    ExprPtr body = parse_block_or_expr();
    SourceSpan span = SourceSpan::merge(kw.span, body->span);
    return std::make_shared<Expr>(
        Expr{ExprLet{is_rec, std::move(pat), std::move(ann), std::move(val), std::move(body)},
             span});
}

ExprPtr Parser::parse_fn_expr() {
    const Token& kw = expect(TokenKind::KwFn, "expected 'fn'");
    std::vector<Param> params;
    std::optional<TypeExprPtr> ret_ann;

    if (check(TokenKind::LParen)) {
        size_t saved = pos_;
        const Token& lp = advance();
        if (match(TokenKind::RParen)) {
            const Token& rp = previous();
            SourceSpan uspan = SourceSpan::merge(lp.span, rp.span);
            params.push_back(Param{
                std::make_shared<Pattern>(Pattern{PatUnit{}, uspan}),
                std::make_shared<TypeExpr>(TypeExpr{TCon{"Unit", {}}, uspan}),
                uspan,
            });
        } else {
            // Disambiguate `fn(a: T, b: T)` / `fn(a, b) : Ret:` vs ML `fn (a, b) -> ...`
            bool is_param_list = false;
            int depth = 1;
            bool saw_colon_at_depth1 = false;
            size_t scan = pos_;
            while (scan < tokens_.size() && depth > 0) {
                TokenKind k = tokens_[scan].kind;
                if (k == TokenKind::LParen) ++depth;
                else if (k == TokenKind::RParen) {
                    --depth;
                    if (depth == 0) break;
                } else if (depth == 1 && k == TokenKind::Colon) {
                    saw_colon_at_depth1 = true;
                }
                ++scan;
            }
            if (saw_colon_at_depth1) {
                is_param_list = true;
            } else if (scan + 1 < tokens_.size()) {
                TokenKind after = tokens_[scan + 1].kind;
                if (after == TokenKind::Colon || after == TokenKind::Eq) {
                    is_param_list = true;
                }
            }

            if (is_param_list) {
                ++expr_delim_depth_;
                do {
                    PatternPtr p = parse_pattern();
                    std::optional<TypeExprPtr> pty;
                    SourceSpan pspan = p->span;
                    if (match(TokenKind::Colon)) {
                        pty = parse_type();
                        pspan = SourceSpan::merge(pspan, (*pty)->span);
                    }
                    params.push_back(Param{std::move(p), std::move(pty), pspan});
                } while (match(TokenKind::Comma));
                --expr_delim_depth_;
                expect(TokenKind::RParen, "expected ')' after parameter list");
            } else {
                pos_ = saved;
                do {
                    params.push_back(parse_param());
                } while (!check(TokenKind::Arrow) && !check(TokenKind::Eq) &&
                         !check(TokenKind::Colon) && !at_end());
            }
        }
    } else {
        do {
            params.push_back(parse_param());
        } while (!check(TokenKind::Arrow) && !check(TokenKind::Eq) &&
                 !check(TokenKind::Colon) && !at_end());
    }

    bool body_delim_matched = false;
    if (match(TokenKind::Colon)) {
        if (starts_indented_block()) {
            body_delim_matched = true;
        } else {
            ret_ann = parse_type();
            if (match(TokenKind::Colon) || match(TokenKind::Eq) || match(TokenKind::Arrow)) {
                body_delim_matched = true;
            }
        }
    } else if (match(TokenKind::Arrow) || match(TokenKind::Eq)) {
        body_delim_matched = true;
    }

    if (!body_delim_matched && !starts_indented_block()) {
        error(peek(), "expected '->', '=', ':' or indented block in fn expression");
    }

    ExprPtr body = parse_block_or_expr();
    ExprPtr fn = wrap_fn_params(params, ret_ann, body);
    return std::make_shared<Expr>(Expr{fn->node, SourceSpan::merge(kw.span, body->span)});
}

ExprPtr Parser::parse_if_expr() {
    const Token& kw = expect(TokenKind::KwIf, "expected 'if'");
    ExprPtr cond = parse_expr();
    ExprPtr then_br;
    if (match(TokenKind::KwThen) || match(TokenKind::Eq) || match(TokenKind::Colon)) {
        then_br = parse_block_or_expr();
    } else if (starts_indented_block()) {
        then_br = parse_block();
    } else {
        error(peek(), "expected 'then', '=', ':' or indented block in if expression");
    }

    ExprPtr else_br;
    if (match(TokenKind::KwElse)) {
        if (check(TokenKind::Eq) || check(TokenKind::Colon)) advance();
        else_br = parse_block_or_expr();
    } else if (check(TokenKind::Newline) && peek(1).kind == TokenKind::KwElse) {
        advance();
        advance();
        if (check(TokenKind::Eq) || check(TokenKind::Colon)) advance();
        else_br = parse_block_or_expr();
    } else {
        else_br = std::make_shared<Expr>(Expr{ExprUnit{}, then_br->span});
    }

    SourceSpan span = SourceSpan::merge(kw.span, else_br->span);
    return std::make_shared<Expr>(Expr{ExprIf{cond, then_br, else_br}, span});
}

ExprPtr Parser::parse_match_expr() {
    const Token& kw = expect(TokenKind::KwMatch, "expected 'match'");
    ExprPtr scrutinee = parse_expr();
    if (!match(TokenKind::KwWith) && !match(TokenKind::Eq) && !match(TokenKind::Colon)) {
        error(peek(), "expected 'with', '=', or ':' in match expression");
    }

    bool has_indent = false;
    if (starts_indented_block()) {
        match(TokenKind::Newline);
        expect(TokenKind::Indent, "expected indent in match expression");
        has_indent = true;
    } else {
        match(TokenKind::Newline);
    }
    match(TokenKind::Pipe);

    std::vector<MatchArm> arms;
    auto parse_arm = [&]() {
        PatternPtr pat = parse_pattern();
        expect(TokenKind::Arrow, "expected '->' in match arm");
        bool old_stop = stop_at_pipe_;
        int old_depth = match_arm_delim_depth_;
        stop_at_pipe_ = true;
        match_arm_delim_depth_ = expr_delim_depth_;
        ExprPtr body = parse_block_or_expr();
        stop_at_pipe_ = old_stop;
        match_arm_delim_depth_ = old_depth;
        arms.push_back(MatchArm{pat, body, SourceSpan::merge(pat->span, body->span)});
    };
    parse_arm();
    while (true) {
        if (match(TokenKind::Pipe)) {
            parse_arm();
        } else if (check(TokenKind::Newline) && peek(1).kind == TokenKind::Pipe) {
            advance();
            advance();
            parse_arm();
        } else {
            break;
        }
    }
    if (has_indent) {
        match(TokenKind::Newline);
        expect(TokenKind::Dedent, "expected dedent after match arms");
    }
    SourceSpan span = SourceSpan::merge(kw.span, arms.back().span);
    return std::make_shared<Expr>(Expr{ExprMatch{scrutinee, std::move(arms)}, span});
}

ExprPtr Parser::parse_while_expr() {
    const Token& kw = expect(TokenKind::KwWhile, "expected 'while'");
    ExprPtr cond = parse_expr();
    if (check(TokenKind::Eq) || check(TokenKind::Colon)) advance();
    ExprPtr body = parse_block_or_expr();
    SourceSpan span = SourceSpan::merge(kw.span, body->span);
    return std::make_shared<Expr>(Expr{ExprWhile{cond, body}, span});
}

ExprPtr Parser::parse_for_expr() {
    const Token& kw = expect(TokenKind::KwFor, "expected 'for'");
    if (match(TokenKind::KwEach)) {
        PatternPtr pat = parse_pattern();
        expect(TokenKind::KwIn, "expected 'in' in 'for each'");
        ExprPtr iterable = parse_expr();
        if (check(TokenKind::Eq) || check(TokenKind::Colon)) advance();
        ExprPtr body = parse_block_or_expr();
        SourceSpan span = SourceSpan::merge(kw.span, body->span);
        return std::make_shared<Expr>(Expr{ExprForEach{pat, iterable, body}, span});
    }

    bool has_paren = match(TokenKind::LParen);
    if (has_paren) ++expr_delim_depth_;

    std::optional<StmtPtr> init;
    if (!match(TokenKind::Semicolon)) {
        if (check(TokenKind::KwVar)) {
            init = parse_var_stmt();
        } else {
            init = parse_assign_or_expr();
        }
        expect(TokenKind::Semicolon, "expected ';' after for-loop initializer");
    }

    ExprPtr cond;
    if (match(TokenKind::Semicolon)) {
        cond = std::make_shared<Expr>(Expr{ExprBool{true}, kw.span});
    } else {
        cond = parse_expr();
        expect(TokenKind::Semicolon, "expected ';' after for-loop condition");
    }

    std::optional<ExprPtr> step;
    bool at_step_end = has_paren ? check(TokenKind::RParen)
                                 : (starts_indented_block() || check(TokenKind::Newline));
    if (!at_step_end) {
        StmtPtr step_stmt = parse_assign_or_expr();
        if (auto* es = std::get_if<StmtExpr>(&step_stmt->node)) {
            step = es->expr;
        }
    }

    if (has_paren) {
        --expr_delim_depth_;
        expect(TokenKind::RParen, "expected ')' after for-loop clauses");
    }

    if (check(TokenKind::Eq) || check(TokenKind::Colon)) advance();
    ExprPtr body = parse_block_or_expr();
    SourceSpan span = SourceSpan::merge(kw.span, body->span);
    return std::make_shared<Expr>(
        Expr{ExprFor{std::move(init), std::move(cond), std::move(step), std::move(body)}, span});
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
        case TokenKind::Pipe: return BinOpInfo{BinaryOp::BitOr, 4, false};
        case TokenKind::Caret: return BinOpInfo{BinaryOp::BitXor, 5, false};
        case TokenKind::Amp: return BinOpInfo{BinaryOp::BitAnd, 6, false};
        case TokenKind::EqEq: return BinOpInfo{BinaryOp::Eq, 7, false};
        case TokenKind::BangEq: return BinOpInfo{BinaryOp::Neq, 7, false};
        case TokenKind::Lt: return BinOpInfo{BinaryOp::Lt, 7, false};
        case TokenKind::LtEq: return BinOpInfo{BinaryOp::Le, 7, false};
        case TokenKind::Gt: return BinOpInfo{BinaryOp::Gt, 7, false};
        case TokenKind::GtEq: return BinOpInfo{BinaryOp::Ge, 7, false};
        case TokenKind::LtLt: return BinOpInfo{BinaryOp::Shl, 8, false};
        case TokenKind::GtGt: return BinOpInfo{BinaryOp::Shr, 8, false};
        case TokenKind::ColonColon: return BinOpInfo{BinaryOp::Cons, 9, true};
        case TokenKind::Plus: return BinOpInfo{BinaryOp::AddInt, 10, false};
        case TokenKind::Minus: return BinOpInfo{BinaryOp::SubInt, 10, false};
        case TokenKind::PlusDot: return BinOpInfo{BinaryOp::AddFloat, 10, false};
        case TokenKind::MinusDot: return BinOpInfo{BinaryOp::SubFloat, 10, false};
        case TokenKind::PlusPlus: return BinOpInfo{BinaryOp::ConcatStr, 10, false};
        case TokenKind::Star: return BinOpInfo{BinaryOp::MulInt, 11, false};
        case TokenKind::Slash: return BinOpInfo{BinaryOp::DivInt, 11, false};
        case TokenKind::Percent: return BinOpInfo{BinaryOp::ModInt, 11, false};
        case TokenKind::StarDot: return BinOpInfo{BinaryOp::MulFloat, 11, false};
        case TokenKind::SlashDot: return BinOpInfo{BinaryOp::DivFloat, 11, false};
        default: return std::nullopt;
    }
}

ExprPtr Parser::parse_binary(int min_prec) {
    auto left = parse_unary();
    while (true) {
        if (check(TokenKind::Newline) && peek(1).kind == TokenKind::PipeGt) {
            advance();
        }
        auto info = lookup_binop(peek().kind);
        if (!info || info->prec < min_prec) {
            break;
        }
        if (info->op == BinaryOp::BitOr && stop_at_pipe_ &&
            expr_delim_depth_ == match_arm_delim_depth_) {
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
    if (check(TokenKind::Minus) || check(TokenKind::MinusDot) || check(TokenKind::Tilde) ||
        check(TokenKind::Bang) || check(TokenKind::Star) || check(TokenKind::Amp)) {
        const Token& op_tok = advance();
        UnaryOp op = UnaryOp::NegInt;
        switch (op_tok.kind) {
            case TokenKind::Minus: op = UnaryOp::NegInt; break;
            case TokenKind::MinusDot: op = UnaryOp::NegFloat; break;
            case TokenKind::Tilde: op = UnaryOp::BitNot; break;
            case TokenKind::Bang: op = UnaryOp::LogicalNot; break;
            case TokenKind::Star: op = UnaryOp::Deref; break;
            case TokenKind::Amp: op = UnaryOp::AddrOf; break;
            default: break;
        }
        auto operand = parse_unary();
        SourceSpan span = SourceSpan::merge(op_tok.span, operand->span);
        return std::make_shared<Expr>(Expr{ExprUnary{op, operand}, span});
    }
    if (match(TokenKind::KwNew)) {
        const Token& kw = previous();
        auto operand = parse_unary();
        SourceSpan span = SourceSpan::merge(kw.span, operand->span);
        return std::make_shared<Expr>(Expr{ExprNew{std::move(operand)}, span});
    }
    if (match(TokenKind::KwClose)) {
        const Token& kw = previous();
        auto operand = parse_unary();
        SourceSpan span = SourceSpan::merge(kw.span, operand->span);
        return std::make_shared<Expr>(Expr{ExprClose{std::move(operand)}, span});
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
        case TokenKind::KwImport:
        case TokenKind::KwBreak:
        case TokenKind::KwContinue:
        case TokenKind::KwBegin:
        case TokenKind::Dollar:
            return true;
        default:
            return false;
    }
}

ExprPtr Parser::parse_postfix() {
    auto expr = parse_atom();
    while (check(TokenKind::Dot) || check(TokenKind::Arrow)) {
        if (match(TokenKind::Dot)) {
            const Token& field_tok =
                (check(TokenKind::LowerIdent) || check(TokenKind::UpperIdent))
                    ? advance()
                    : expect(TokenKind::LowerIdent, "expected field or member name after '.'");
            SourceSpan span = SourceSpan::merge(expr->span, field_tok.span);
            expr = std::make_shared<Expr>(
                Expr{ExprFieldGet{expr, Ident{field_tok.lexeme, {}, field_tok.span}}, span});
        } else {
            advance();  // '->'
            const Token& field_tok =
                (check(TokenKind::LowerIdent) || check(TokenKind::UpperIdent))
                    ? advance()
                    : expect(TokenKind::LowerIdent, "expected field name after '->'");
            SourceSpan span = SourceSpan::merge(expr->span, field_tok.span);
            expr = std::make_shared<Expr>(
                Expr{ExprPtrFieldGet{expr, Ident{field_tok.lexeme, {}, field_tok.span}}, span});
        }
    }
    return expr;
}

ExprPtr Parser::parse_app() {
    auto fn = parse_postfix();
    while (starts_atom()) {
        if (expr_delim_depth_ == 0 && top_stmt_col_ > 0 &&
            peek().span.line > previous().span.line && peek().span.column <= top_stmt_col_) {
            break;
        }
        auto arg = parse_postfix();
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
    if (match(TokenKind::KwBreak)) {
        return std::make_shared<Expr>(Expr{ExprBreak{}, tok.span});
    }
    if (match(TokenKind::KwContinue)) {
        return std::make_shared<Expr>(Expr{ExprContinue{}, tok.span});
    }
    if (match(TokenKind::KwImport)) {
        const Token& ptok = expect(TokenKind::String, "expected module path string after 'import'");
        return std::make_shared<Expr>(
            Expr{ExprImport{ptok.str_val}, SourceSpan::merge(tok.span, ptok.span)});
    }
    auto parse_struct_lit = [&](Ident id, const SourceSpan& start_span) -> ExprPtr {
        ++expr_delim_depth_;
        std::vector<std::pair<Ident, ExprPtr>> fields;
        if (!check(TokenKind::RBrace)) {
            while (!check(TokenKind::RBrace) && !at_end()) {
                match(TokenKind::Dot);
                const Token& fn_tok =
                    expect(TokenKind::LowerIdent, "expected field name in struct literal");
                expect(TokenKind::Eq, "expected '=' after struct field name");
                ExprPtr fv = parse_expr();
                fields.emplace_back(Ident{fn_tok.lexeme, {}, fn_tok.span}, std::move(fv));
                if (!match(TokenKind::Comma) && !match(TokenKind::Semicolon)) {
                    if (!check(TokenKind::Dot) && !check(TokenKind::LowerIdent)) break;
                }
            }
        }
        --expr_delim_depth_;
        const Token& rb = expect(TokenKind::RBrace, "expected '}' after struct literal");
        return std::make_shared<Expr>(
            Expr{ExprStructLit{std::move(id), std::move(fields)},
                 SourceSpan::merge(start_span, rb.span)});
    };
    if (match(TokenKind::LowerIdent)) {
        Ident id{tok.lexeme, {}, tok.span};
        if (match(TokenKind::LBrace)) {
            return parse_struct_lit(std::move(id), tok.span);
        }
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
        Ident id{tok.lexeme, {}, tok.span};
        if (match(TokenKind::LBrace)) {
            return parse_struct_lit(std::move(id), tok.span);
        }
        return std::make_shared<Expr>(Expr{ExprCtor{std::move(id)}, tok.span});
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
