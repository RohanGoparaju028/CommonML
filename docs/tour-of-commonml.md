---
layout: default
title: A Tour of CommonML
---

# A tour of CommonML

CommonML blends **ML-family functional programming** (Hindley-Milner type inference, algebraic data types, closures, and pattern matching) with **explicit systems programming constructs** (fixed-width numerics, pointers `*T`, struct field indirection `->`, heap `new`, `close`, and scoped `defer`).

## Your first program

```text
let main = func() -> i32:
             println "Hello from CommonML!"
             0

main ()
```

Save as `hello.cml` and either run directly or compile:

```sh
cml run hello.cml
cmlc hello.cml
./hello
```

## Structs, heap allocation (`new`), pointers (`->`), and `defer`

Structs use indented `field: type` blocks, and heap allocations via `new` return a pointer `*rect` whose fields are accessed with `->`:

```text
let rect = struct:
             x: i32
             y: i32
             label: str
             active: bool

let area = func() -> i32:
             let var_rect = new rect { .x = 10, .y = 20, .label = "box", .active = true }
             defer close var_rect
             if var_rect->active =
               var_rect->x * var_rect->y
             else =
               0

area ()
```

Key design rules:

1. **Stack vs. heap**: `rect { .x = 1, .y = 2 }` creates a value (`r.x`), whereas `new rect { ... }` allocates on the heap and returns `*rect` (`r->x`).
2. **Deterministic cleanup**: `defer close var_rect` guarantees `var_rect` is freed right before `area` returns `200`.
3. **Memory safety**: Accessing or double-closing a pointer after `close` immediately raises a safe runtime error instead of silent corruption.

## Functional features

CommonML retains full Hindley-Milner polymorphism, algebraic data types, and curried closures:

```text
type Option a =
  | None
  | Some a

let unwrap_or d opt =
  match opt with
  | None -> d
  | Some x -> x
```

## Next steps

- [Installers & Environment Setup](getting-started/installation.html)
- [Types & Data Structures](fundamentals/types.html)
- [Memory Safety, Pointers, new & defer](fundamentals/memory-and-pointers.html)
