# CommonML 

CommonML (`cml`) is a statically typed, Hindley-Milner functional programming language implemented in C++20 with algebraic data types, pattern matching, hygienic AST macros (`quote` / `$`), a bytecode compiler, a stack-based VM with closures and tail-call optimization, and an interactive REPL.

## Features

- **Hindley-Milner Type Inference**: Automatic type inference with occurs check, `let`-polymorphism, and optional type annotations.
- **Algebraic Data Types & Pattern Matching**: Polymorphic and recursive `type` declarations, curried constructors, and `match ... with` destructuring for literals, tuples, lists, cons (`::`), and ADTs.
- **First-Class Curried Functions**: Closures with lexical upvalues, `let rec` recursion, pipe-forward (`|>`), and tail-call optimization (`TailCall`).
- **Hygienic Macros**: Compile-time `macro` definitions using `quote { ... }` and `$` / `$(...)` splicing, backed by Flatt-style sets-of-scopes (`ScopeId`) hygiene to prevent variable capture.
- **Bytecode Compiler & Stack VM**: Compiles expanded, typechecked ASTs to a compact bytecode instruction set with a disassembler (`--bc` / `:bc`).
- **CLI, Package Manager (`build.cml`) & Interactive REPL**: Run `.cml` scripts (`cml run`), compile standalone bytecode executables (`cml build` / `cmlc`), syntax-highlight code (`--highlight` / `:highlight` plus TextMate grammar in `editors/vscode/`), scaffold `lib` / `console` / `web` projects with Zig-style `build.cml` scripts (`new`, `init`, `add`, `install`, `build`, `run`, `test`, `pkg list`), or explore interactively in the REPL.
- **Reference-Counted Memory, Indentation Blocks, Loops & Collections**: Intrusive reference counting (`rc_count`, `rc_live_objects`), indentation-based blocks for functions and control flow, `struct` declarations and dot-access (`p.x`), assignment imports (`let math = import "std/math"`), `var` mutable bindings, `while` / C-style `for` / `for each` loops (`break`, `continue`), `Vector`/`Dict`/`Set` collections, and file/socket I/O.
- **Standard Library & Bootstrap Compiler**: Includes `std/core`, `std/math`, `std/console`, `std/net` (TCP sockets), and `std/http` (HTTP/1.1 server/client), plus a self-hosted bootstrap CommonML compiler/interpreter in `bootstrap/`.

## Building & Testing

Requires CMake 3.16+ and a C++20 compiler.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Quick Start

### Compile & Run (`cml` / `cmlc`)

```sh
# Like `go run`
./build/cml run hello.cml
./build/cml run hello.cml arg1 arg2

# Like `go build` (or `cmlc hello.cml`) -> generates ./hello
./build/cml build hello.cml
./hello
```

Or install globally (`cml`, `commonml`, `cmlc`, `stdlib/`) and run without paths:

```sh
cmake --build build -j
sudo cmake --install build
cml run hello.cml
```

### Evaluate Inline Code

```sh
./build/commonml -e '
let rec fib n =
  if n <= 1 then n else fib (n - 1) + fib (n - 2)
fib 10
'
# Output: 55
```

### Algebraic Data Types & Pipelines

```sh
./build/commonml -e '
type Option a = None | Some a

let unwrap_or d opt =
  match opt with
  | None -> d
  | Some x -> x

let safe_div a b =
  if b == 0 then None else Some (a / b)

safe_div 84 2 |> unwrap_or 0
'
# Output: 42
```

### Hygienic Macros

```sh
./build/commonml -e '
macro with_temp v body =
  quote {
    let x = $v + 1 in
    $body + x
  }

let x = 100
with_temp!(5, x)
'
# Output: 106 (caller x is not captured by macro-local x)
```

### Start the REPL

```sh
./build/commonml
```

REPL commands include `:type <expr>`, `:ast`, `:bc`, `:reset`, `:help`, and `:quit`.

### Package Manager (`build.cml`) & Project Templates

Create libraries, console apps, or HTTP web applications with Zig-style `build.cml` configuration scripts:

```sh
./build/cml new lib my_lib
./build/cml new console my_cli
./build/cml new web my_web

cd my_cli
../build/cml add my_lib --path ../my_lib
../build/cml build
../build/cml test
../build/cml run -- arg1 arg2
```

### Self-Hosted Bootstrap Compiler

Compile and run CommonML programs using the self-hosted lexer, parser, Hindley-Milner typechecker, and bytecode compiler written in CommonML (targeting the CommonML stack VM via `.cmlbc` bytecode):

```sh
./build/commonml bootstrap/main.cml
./build/commonml bootstrap/main.cml -- -e 'let rec fact n = if n <= 1 then 1 else n * fact (n - 1)
fact 6'
./build/commonml bootstrap/main.cml -- -c app.cml -o app.cmlbc
./build/commonml app.cmlbc
```

## Project Layout

```text
include/cml/
  source.hpp          Source spans & formatted diagnostics
  token.hpp           Token definitions
  lexer.hpp           Lexer interface
  ast.hpp             AST nodes, ScopeId, ScopeEnv
  parser.hpp          Recursive-descent & Pratt parser
  type.hpp            Type representation, substitutions, unification API
  typecheck.hpp       Hindley-Milner type checker
  value.hpp           Runtime value representation
  builtins.hpp        Built-in function definitions
  bytecode.hpp        Bytecode opcodes, Chunk, FunctionProto, disassembler
  compiler.hpp        Bytecode compiler
  vm.hpp              Stack-based bytecode VM
  macro_expander.hpp  Hygienic macro expander
  pipeline.hpp        End-to-end compilation & execution pipeline
  repl.hpp            Interactive REPL

src/
  source.cpp, lexer.cpp, ast.cpp, parser.cpp
  types/              type.cpp, unify.cpp, typecheck_env.cpp, typecheck_pat.cpp, typecheck_expr.cpp
  runtime/            value.cpp, builtins.cpp
  bytecode/           bytecode.cpp
  codegen/            compiler_state.cpp, compiler_pat.cpp, compiler_expr.cpp
  vm/                 vm.cpp, vm_ops.cpp
  macro/              macro_expander.cpp
  driver/             pipeline.cpp, repl.cpp
  main.cpp

tests/
  test_framework.hpp
  test_lexer_parser.cpp
  test_typecheck.cpp
  test_macros.cpp
  test_vm.cpp
  test_main.cpp
```

## Documentation

- [Language Reference](docs/language.md)
- [Hygienic Macros](docs/macros.md)
- [Compiler & VM Architecture](docs/architecture.md)
- [CLI & REPL Guide](docs/cli-repl.md)
