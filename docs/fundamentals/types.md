---
layout: default
title: Type System and Data Structures
---

# Type System & Data Structures

CommonML uses **Hindley-Milner type inference** paired with systems primitive types and user-defined structures.

## Numerics, Booleans, and Strings

| Category | Types | Notes |
| --- | --- | --- |
| Signed Integers | `Int`, `i8`, `i16`, `i32`, `i64`, `isize` | Integer literals unify with annotated widths; unannotated defaults to `Int` |
| Unsigned Integers | `u8`, `u16`, `u32`, `u64`, `usize` | Checked width equality (`i32` != `i64`) |
| Floating Point | `Float`, `f32`, `f64` | Float operators: `+.`, `-.`, `*.`, `/.` |
| Boolean | `Bool`, `bool` | `true`, `false`, `!x`, `&&`, `||` |
| String | `String`, `str`, `string` | UTF-8 strings (`"..."`), concatenation `++` |
| Unit | `Unit`, `unit`, `void` | `()` |
| Pointers | `*T` (e.g. `*rect`, `*i32`) | Created via `new` or `&` |

## Bitwise & Arithmetic Operators

- Integer arithmetic: `+`, `-`, `*`, `/`, `%`, unary `-`
- Bitwise operations: `&` (AND), `|` (OR), `^` (XOR), `~` (NOT), `<<` (left shift), `>>` (right shift)
- Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`

## Structs

Structs are assigned with `let <name> = struct:` with fields indented relative to the `struct` keyword:

```text
let rect = struct:
             x: i32
             y: i32

let r1 = rect { .x = 10, .y = 20 }
let p1 = r1.x * r1.y
```

## Algebraic Data Types & Pattern Matching

```text
type Result a e =
  | Ok a
  | Err e
```

## Collections

- Immutable singly-linked `List a`: `[1, 2, 3]`, `h :: t`
- Mutable reference-counted `Vector a`: `vec_new ()`, `vec_push`, `vec_pop`, `vec_get`, `vec_set`, `vec_len`
- Mutable `Dict k v`: `dict_new ()`, `dict_set`, `dict_get`, `dict_remove`, `dict_has`, `dict_len`
- Mutable `Set a`: `set_new ()`, `set_add`, `set_has`, `set_remove`, `set_len`
