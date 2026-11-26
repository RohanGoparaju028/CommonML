---
layout: default
title: Hygienic Macros
---

# Hygienic Macros

CommonML supports compile-time AST macros with Flatt-style sets-of-scopes hygiene.

## Defining and Invoking Macros

Macros are declared with `macro` and invoked with `name!(...)`:

```ocaml
macro swap_if cond a b =
  quote { if $cond then $b else $a }

swap_if!(true, 10, 20)
```

- `quote { ... }` constructs an AST template (`ValAstExpr` at macro-expansion time).
- `$var` and `$(expr)` splice AST values (or primitive literals coerced to AST nodes) into expression or pattern positions.

## Hygiene

Each macro expansion allocates a fresh `ScopeId`:

1. The expander flips that scope on every argument passed to the macro.
2. The macro body executes in the compile-time VM, instantiating `quote { ... }` templates with the spliced arguments.
3. The expander flips the same scope on the resulting AST.

Identifiers originating from caller arguments are flipped twice (net unchanged), while binders and references introduced by `quote { ... }` keep the macro call's `ScopeId`. Variable lookup uses `Ident::is_subset_of`, picking the most specific matching scope set so macro-local bindings never accidentally capture caller variables:

```ocaml
macro with_temp v body =
  quote {
    let x = $v + 1 in
    $body + x
  }

let x = 100
with_temp!(5, x)
-- Evaluates to 106 (100 + 6), not 12
```

## Nested Macro Expansion

Macro outputs are recursively expanded (up to a depth limit of 256), allowing macros to emit calls to other macros or to themselves.
