#include "cml/compiler.hpp"

namespace cml {

void Compiler::collect_pattern_binders(const PatternPtr& pat, std::vector<Ident>& out) const {
    if (!pat) return;
    std::visit(
        [&](const auto& n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, PatVar>) {
                out.push_back(n.ident);
            } else if constexpr (std::is_same_v<T, PatTuple>) {
                for (const auto& e : n.elems) collect_pattern_binders(e, out);
            } else if constexpr (std::is_same_v<T, PatList>) {
                for (const auto& e : n.elems) collect_pattern_binders(e, out);
            } else if constexpr (std::is_same_v<T, PatCons>) {
                collect_pattern_binders(n.head, out);
                collect_pattern_binders(n.tail, out);
            } else if constexpr (std::is_same_v<T, PatCtor>) {
                for (const auto& a : n.args) collect_pattern_binders(a, out);
            }
        },
        pat->node);
}

void Compiler::compile_pattern_match(const PatternPtr& pat, std::vector<size_t>& fail_jumps) {
    if (!pat) {
        emit(OpCode::Pop);
        return;
    }

    std::visit(
        [&](const auto& n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, PatWildcard>) {
                emit(OpCode::Pop, 0, 0, pat->span);
            } else if constexpr (std::is_same_v<T, PatVar>) {
                uint32_t slot = alloc_local(n.ident);
                emit(OpCode::SetLocal, static_cast<int32_t>(slot), 0, pat->span);
            } else if constexpr (std::is_same_v<T, PatInt>) {
                uint32_t idx = chunk().add_constant(Value::make_int(n.value));
                fail_jumps.push_back(emit(OpCode::MatchInt, static_cast<int32_t>(idx), 0, pat->span));
            } else if constexpr (std::is_same_v<T, PatFloat>) {
                uint32_t idx = chunk().add_constant(Value::make_float(n.value));
                fail_jumps.push_back(emit(OpCode::MatchFloat, static_cast<int32_t>(idx), 0, pat->span));
            } else if constexpr (std::is_same_v<T, PatBool>) {
                fail_jumps.push_back(
                    emit(OpCode::MatchBool, n.value ? 1 : 0, 0, pat->span));
            } else if constexpr (std::is_same_v<T, PatString>) {
                uint32_t idx = chunk().add_constant(Value::make_string(n.value));
                fail_jumps.push_back(emit(OpCode::MatchString, static_cast<int32_t>(idx), 0, pat->span));
            } else if constexpr (std::is_same_v<T, PatUnit>) {
                fail_jumps.push_back(emit(OpCode::MatchUnit, 0, 0, pat->span));
            } else if constexpr (std::is_same_v<T, PatTuple>) {
                fail_jumps.push_back(
                    emit(OpCode::MatchTuple, static_cast<int32_t>(n.elems.size()), 0, pat->span));
                std::vector<size_t> sub_fails;
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    emit(OpCode::TupleGet, static_cast<int32_t>(i), 0, n.elems[i]->span);
                    compile_pattern_match(n.elems[i], sub_fails);
                }
                emit(OpCode::Pop, 0, 0, pat->span);
                if (!sub_fails.empty()) {
                    size_t ok_jump = emit_jump(OpCode::Jump, pat->span);
                    for (size_t j : sub_fails) patch_jump(j);
                    emit(OpCode::Pop, 0, 0, pat->span);
                    fail_jumps.push_back(emit_jump(OpCode::Jump, pat->span));
                    patch_jump(ok_jump);
                }
            } else if constexpr (std::is_same_v<T, PatList>) {
                fail_jumps.push_back(
                    emit(OpCode::MatchListLen, static_cast<int32_t>(n.elems.size()), 0, pat->span));
                std::vector<size_t> sub_fails;
                for (size_t i = 0; i < n.elems.size(); ++i) {
                    emit(OpCode::ListGet, static_cast<int32_t>(i), 0, n.elems[i]->span);
                    compile_pattern_match(n.elems[i], sub_fails);
                }
                emit(OpCode::Pop, 0, 0, pat->span);
                if (!sub_fails.empty()) {
                    size_t ok_jump = emit_jump(OpCode::Jump, pat->span);
                    for (size_t j : sub_fails) patch_jump(j);
                    emit(OpCode::Pop, 0, 0, pat->span);
                    fail_jumps.push_back(emit_jump(OpCode::Jump, pat->span));
                    patch_jump(ok_jump);
                }
            } else if constexpr (std::is_same_v<T, PatCons>) {
                fail_jumps.push_back(emit(OpCode::MatchCons, 0, 0, pat->span));
                std::vector<size_t> sub_fails;
                emit(OpCode::ListGet, 0, 0, n.head->span);
                compile_pattern_match(n.head, sub_fails);
                emit(OpCode::ListTail, 1, 0, n.tail->span);
                compile_pattern_match(n.tail, sub_fails);
                emit(OpCode::Pop, 0, 0, pat->span);
                if (!sub_fails.empty()) {
                    size_t ok_jump = emit_jump(OpCode::Jump, pat->span);
                    for (size_t j : sub_fails) patch_jump(j);
                    emit(OpCode::Pop, 0, 0, pat->span);
                    fail_jumps.push_back(emit_jump(OpCode::Jump, pat->span));
                    patch_jump(ok_jump);
                }
            } else if constexpr (std::is_same_v<T, PatCtor>) {
                if (!ctors_) {
                    throw CompileError(Diagnostic{"codegen", "missing constructor table", pat->span});
                }
                auto it = ctors_->find(n.ctor.name);
                if (it == ctors_->end()) {
                    throw CompileError(
                        Diagnostic{"codegen", "unknown constructor '" + n.ctor.name + "'", pat->span});
                }
                fail_jumps.push_back(
                    emit(OpCode::MatchAdt, static_cast<int32_t>(it->second.tag), 0, pat->span));
                std::vector<size_t> sub_fails;
                for (size_t i = 0; i < n.args.size(); ++i) {
                    emit(OpCode::AdtField, static_cast<int32_t>(i), 0, n.args[i]->span);
                    compile_pattern_match(n.args[i], sub_fails);
                }
                emit(OpCode::Pop, 0, 0, pat->span);
                if (!sub_fails.empty()) {
                    size_t ok_jump = emit_jump(OpCode::Jump, pat->span);
                    for (size_t j : sub_fails) patch_jump(j);
                    emit(OpCode::Pop, 0, 0, pat->span);
                    fail_jumps.push_back(emit_jump(OpCode::Jump, pat->span));
                    patch_jump(ok_jump);
                }
            } else {
                throw CompileError(
                    Diagnostic{"codegen", "unquote pattern outside quote template", pat->span});
            }
        },
        pat->node);
}

void Compiler::compile_pattern_bind(const PatternPtr& pat, bool is_global) {
    if (!is_global) {
        std::vector<size_t> fails;
        compile_pattern_match(pat, fails);
        if (!fails.empty()) {
            size_t ok = emit_jump(OpCode::Jump, pat->span);
            for (size_t f : fails) patch_jump(f);
            emit(OpCode::MatchFail, 0, 0, pat->span);
            patch_jump(ok);
        }
        return;
    }

    // Global pattern binding: match into temp locals, then copy each bound local into globals.
    size_t mark = current_->locals.size();
    std::vector<Ident> binders;
    collect_pattern_binders(pat, binders);

    std::vector<size_t> fails;
    compile_pattern_match(pat, fails);
    if (!fails.empty()) {
        size_t ok = emit_jump(OpCode::Jump, pat->span);
        for (size_t f : fails) patch_jump(f);
        emit(OpCode::MatchFail, 0, 0, pat->span);
        patch_jump(ok);
    }

    for (size_t i = mark; i < current_->locals.size(); ++i) {
        const auto& loc = current_->locals[i];
        uint32_t name_idx = chunk().add_name(loc.ident.name);
        emit(OpCode::GetLocal, static_cast<int32_t>(loc.slot), 0, pat->span);
        emit(OpCode::DefGlobal, static_cast<int32_t>(name_idx), 0, pat->span);
    }
    pop_locals_to(mark, pat->span);
}

}  // namespace cml
