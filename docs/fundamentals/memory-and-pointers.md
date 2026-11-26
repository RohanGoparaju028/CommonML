---
layout: default
title: Memory Safety, Pointers, new and defer
---

# Memory Safety, Pointers, `new`, `close` & `defer`

CommonML provides deterministic memory management combining **explicit heap ownership (`new`, `close`, `defer`)**, **checked pointers (`*T`, `&`, `*p`, `->`)**, and **intrusive atomic reference counting (`Rc`)** for functional collections.

## Heap Allocation (`new`) & Deallocation (`close` / `defer`)

`new` always allocates on the heap and returns a pointer `*T`:

```text
let rect = struct:
  x: i32
  y: i32

let compute = fn() : i32:
  let var_rect = new rect { .x = 10, .y = 20 }
  defer close var_rect
  var_rect->x * var_rect->y
```

- `new <expr>` allocates a tracked `HeapCell` and increments `heap_live_count ()`.
- `close <ptr>` frees the heap cell immediately and marks it closed.
- `defer <expr>` registers cleanup actions executed in **LIFO order** when leaving the enclosing block, function, loop (`break`/`continue`), or script.

## Value `.` vs Pointer `->` Access & Dereferencing `*p`

- Struct values (`r : rect`) access fields with `.`: `r.x`, `r.y = 5` (if `r` or field is mutated).
- Struct pointers (`p : *rect`) **must** access fields with `->`: `p->x`, `p->y = 5`.
- Using `.` on a `*rect` pointer is rejected at compile time by the typechecker.
- Address-of `&x` creates a pointer to a variable (`var n : i32 = 10; let p = &n; *p = 20`).

## Runtime Memory-Safety Guarantees

- **Use-after-free prevention**: Reading or writing `*p` or `p->field` after `close p` immediately raises a runtime error.
- **Double-close prevention**: Calling `close p` twice raises a runtime error.
- **Stack-escape protection**: Address-of pointers (`&local`) are invalidated when the local's stack frame or block exits, catching dangling dereferences.
- **Reference counting (`Rc`)**: Functional lists, tuples, ADTs, vectors, dicts, and sets use atomic intrusive RC (`rc_count`, `rc_live_objects`).
