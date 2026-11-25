# Compiler & VM Architecture

Every source input flows through a 6-stage pipeline coordinated by `cml::Pipeline`:

```text
SourceFile -> Lexer -> Parser -> MacroExpander -> TypeChecker -> Compiler -> VM
```

## 1. Frontend (`src/source.cpp`, `src/lexer.cpp`, `src/ast.cpp`, `src/parser.cpp`)
- `SourceFile` and `SourceSpan` track byte offsets, line numbers, and columns for rich diagnostics (`Diagnostic::format`).
- `Lexer` produces `Token` streams with support for ML and C-style comments.
- `Parser` uses recursive descent for statements/patterns/types and precedence climbing for binary operators. Top-level statement alignment (`top_stmt_col_`) prevents function application from greedily consuming subsequent top-level statements across lines.

## 2. Macro Expander (`src/macro/macro_expander.cpp`)
- Registers `StmtMacro` definitions by compiling macro bodies into `FunctionProto`s.
- Expands `ExprMacroCall` nodes by executing the compiled macro in an isolated compile-time `VM` and applying hygienic scope flips.

## 3. Type Checker (`src/types/`)
- `type.cpp`: `TypeVar`, `TypeCon`, `TypeFun`, `TypeTuple`, `TypeScheme`, substitutions, and free-variable queries.
- `unify.cpp`: Unification with occurs check, `instantiate`, and `generalize`.
- `typecheck_env.cpp`, `typecheck_pat.cpp`, `typecheck_expr.cpp`: Hindley-Milner inference with `ScopeEnv<TypeScheme>`, ADT constructor checking, and `let`-polymorphism.

## 4. Bytecode & Codegen (`src/bytecode/`, `src/codegen/`)
- `bytecode.cpp`: `Chunk` constant/name/quote pools, jump patching, and `disassemble_chunk`.
- `compiler_state.cpp`: Lexical local slots, hygienic `Ident` resolution, and upvalue capture descriptors.
- `compiler_pat.cpp`: Compiles nested pattern matching into stack-balanced jump sequences (`MatchInt`, `MatchTuple`, `MatchCons`, `MatchAdt`, etc.).
- `compiler_expr.cpp`: Compiles expressions, curried closures, `match` arms, `let rec` bindings, `quote` templates, and tail calls (`TailCall`).

## 5. Stack VM & Runtime (`src/vm/`, `src/runtime/`)
- `vm.cpp`: Frame-based bytecode interpreter with open/closed `UpvalueObj` tracking, tail-call frame reuse, and automatic partial application for `ValClosure`, `ValNative`, and `ValCtor`.
- `vm_ops.cpp`: Binary operators and runtime `quote` slot instantiation (`MakeQuote`).
- `value.cpp` & `builtins.cpp`: Structural equality/comparison, formatting, and native built-ins.
