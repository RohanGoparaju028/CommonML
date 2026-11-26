#include "cml/compiler.hpp"

#include <algorithm>

namespace cml {

Compiler::Compiler(const std::unordered_map<std::string, CtorInfo>* ctors)
    : ctors_(ctors) {}

void Compiler::begin_fn(const std::string& name, size_t arity) {
    auto* state = new FnState();
    state->proto = std::make_shared<FunctionProto>();
    state->proto->name = name;
    state->proto->arity = arity;
    state->enclosing = current_;
    current_ = state;
}

std::shared_ptr<FunctionProto> Compiler::end_fn() {
    FnState* finished = current_;
    current_ = finished->enclosing;
    finished->proto->num_locals = finished->max_locals;
    finished->proto->upvalues = std::move(finished->upvalues);
    auto proto = std::move(finished->proto);
    delete finished;
    return proto;
}

Chunk& Compiler::chunk() {
    return current_->proto->chunk;
}

uint32_t Compiler::alloc_local(const Ident& ident) {
    uint32_t slot = static_cast<uint32_t>(current_->locals.size());
    current_->locals.push_back(LocalVar{ident, slot, false});
    if (chunk().local_names.size() <= slot) {
        chunk().local_names.resize(slot + 1);
    }
    chunk().local_names[slot] = ident.name;
    if (current_->locals.size() > current_->max_locals) {
        current_->max_locals = static_cast<uint32_t>(current_->locals.size());
    }
    return slot;
}

int Compiler::resolve_local(FnState* fn, const Ident& ident) const {
    int best_slot = -1;
    size_t best_scopes = 0;
    for (size_t i = fn->locals.size(); i-- > 0;) {
        const auto& loc = fn->locals[i];
        if (loc.ident.is_subset_of(ident)) {
            if (best_slot == -1 || loc.ident.scopes.size() > best_scopes) {
                best_slot = static_cast<int>(loc.slot);
                best_scopes = loc.ident.scopes.size();
            }
        }
    }
    return best_slot;
}

uint32_t Compiler::add_upvalue(FnState* fn, bool is_local, uint32_t index) {
    for (size_t i = 0; i < fn->upvalues.size(); ++i) {
        if (fn->upvalues[i].is_local == is_local && fn->upvalues[i].index == index) {
            return static_cast<uint32_t>(i);
        }
    }
    fn->upvalues.push_back(UpvalueDesc{is_local, index});
    return static_cast<uint32_t>(fn->upvalues.size() - 1);
}

int Compiler::resolve_upvalue(FnState* fn, const Ident& ident) {
    if (fn->enclosing == nullptr) {
        return -1;
    }
    int local = resolve_local(fn->enclosing, ident);
    if (local != -1) {
        fn->enclosing->locals[static_cast<size_t>(local)].is_captured = true;
        return static_cast<int>(add_upvalue(fn, true, static_cast<uint32_t>(local)));
    }
    int upval = resolve_upvalue(fn->enclosing, ident);
    if (upval != -1) {
        return static_cast<int>(add_upvalue(fn, false, static_cast<uint32_t>(upval)));
    }
    return -1;
}

size_t Compiler::emit(OpCode op, int32_t a1, int32_t a2, const SourceSpan& span) {
    return chunk().emit(op, a1, a2, span);
}

size_t Compiler::emit_jump(OpCode op, const SourceSpan& span) {
    return chunk().emit(op, 0, 0, span);
}

void Compiler::patch_jump(size_t idx) {
    chunk().patch_jump(idx, static_cast<int32_t>(chunk().code.size()));
}

void Compiler::pop_locals_to(size_t count, const SourceSpan& span) {
    while (current_->locals.size() > count) {
        const auto& loc = current_->locals.back();
        if (loc.is_captured) {
            emit(OpCode::CloseUpvalue, static_cast<int32_t>(loc.slot), 0, span);
        }
        current_->locals.pop_back();
    }
}

void Compiler::emit_defer_actions(const std::vector<ExprPtr>& actions, const SourceSpan& span) {
    for (size_t i = actions.size(); i-- > 0;) {
        compile_expr_impl(actions[i], false);
        emit(OpCode::Pop, 0, 0, span);
    }
}

void Compiler::emit_defers_down_to(size_t target_depth, const SourceSpan& span) {
    for (size_t i = current_->defer_frames.size(); i > target_depth; --i) {
        emit_defer_actions(current_->defer_frames[i - 1].actions, span);
    }
}

}  // namespace cml
