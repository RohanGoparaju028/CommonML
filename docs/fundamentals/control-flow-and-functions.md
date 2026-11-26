---
layout: default
title: Functions, Indentation Blocks and Control Flow
---

# Functions, Indentation Blocks & Control Flow

CommonML uses whitespace-significant indentation after `=` and `:` block headers.

## Function Definitions

Both ML curried bindings and systems-style typed signatures are supported:

```text
-- Systems-style signature with explicit parameter & return types
let add = fn(a: i32, b: i32) : i32:
  a + b

let area = fn() : i32:
  10 * 20

-- ML-style curried definition
let rec fact n =
  if n <= 1 = 1
  else = n * fact (n - 1)
```

## Uniform `=` / `:` Control Flow

### `if` / `else`

```text
if x > 0 =
  println "positive"
else =
  println "non-positive"
```

(`then` is also accepted for compatibility with ML-style expressions.)

### `match` Pattern Matching

```text
match opt =
| None -> 0
| Some v -> v
```

(`match opt with` is equally valid.)

### Three Loops (`while`, `for`, `for each`)

```text
var i = 0
while i < 5 =
  println i
  i = i + 1

for (var j = 0; j < 5; j = j + 1) =
  if j == 2 = continue
  println j

for each item in [10, 20, 30] =
  println item
```

`break` and `continue` automatically run inner `defer` cleanup actions before jumping.
