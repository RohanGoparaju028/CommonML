# CommonML Language Reference

## Comments

- Line comments: `-- comment` or `// comment`
- Block comments: `(* nested (* block *) comment *)` or `/* block comment */`

## Primitives & Literals

| Type | Examples |
| --- | --- |
| `Int` | `0`, `42`, `1_000_000`, `-7` |
| `Float` | `3.14`, `1e-5`, `2.5e3`, `-.1.5` |
| `Bool` | `true`, `false` |
| `String` | `"hello\nworld"` (escapes: `\n`, `\t`, `\r`, `\\`, `\"`, `\0`) |
| `Unit` | `()` |
| `List a` | `[]`, `[1, 2, 3]`, `1 :: [2, 3]` |
| Tuples | `(1, "a", true)` |

## Operators

| Operator | Types | Notes |
| --- | --- | --- |
| `+`, `-`, `*`, `/`, `%`, unary `-` | `Int` | Integer arithmetic |
| `+.`, `-.`, `*.`, `/.`, unary `-.` | `Float` | Float arithmetic |
| `++` | `String -> String -> String` | String concatenation |
| `::` | `'a -> List 'a -> List 'a` | Right-associative list cons |
| `==`, `!=`, `<`, `<=`, `>`, `>=` | `'a -> 'a -> Bool` | Structural comparison |
| `&&`, `||` | `Bool -> Bool -> Bool` | Short-circuiting |
| `|>` | `'a -> ('a -> 'b) -> 'b` | Pipe forward |
| `;` | `'a -> 'b -> 'b` | Sequence (evaluates left, returns right) |

## Bindings & Functions

Top-level statements:

```ocaml
let x = 10
let add a b = a + b
let rec fact n = if n <= 1 then 1 else n * fact (n - 1)
```

Local bindings and anonymous functions:

```ocaml
let area r =
  let pi = 3.14159 in
  pi *. r *. r

let double = fn x -> x * 2
let typed_add = fn (x : Int) (y : Int) -> x + y
```

Grouping sequences:

```ocaml
begin
  println "computing...";
  40 + 2
end
```

## Algebraic Data Types & Pattern Matching

```ocaml
type Tree a =
  | Leaf
  | Node (Tree a) a (Tree a)

let rec sum_tree t =
  match t with
  | Leaf -> 0
  | Node l v r -> sum_tree l + v + sum_tree r
```

Supported patterns:
- Wildcard `_` and variable `x`
- Literals (`42`, `3.14`, `true`, `"hi"`, `()`)
- Tuples `(a, b)`
- Lists `[a, b]` and cons `h :: t`
- Constructors `Some x`, `Node l v r`

## Built-in Functions

| Name | Type | Description |
| --- | --- | --- |
| `print` | `'a -> Unit` | Prints value without trailing newline |
| `println` | `'a -> Unit` | Prints value with trailing newline |
| `to_string` | `'a -> String` | Converts any value to a string |
| `not` | `Bool -> Bool` | Boolean negation |
| `abs` | `Int -> Int` | Integer absolute value |
| `int_of_float` | `Float -> Int` | Truncates float to integer |
| `float_of_int` | `Int -> Float` | Converts integer to float |
| `str_len` | `String -> Int` | String length in bytes |
| `str_sub` | `String -> Int -> Int -> String` | Substring `(s, start, len)` |
| `list_len` | `List 'a -> Int` | Number of list elements |
| `is_empty` | `List 'a -> Bool` | Checks if list is empty |
| `head` | `List 'a -> 'a` | First element of non-empty list |
| `tail` | `List 'a -> List 'a` | List without first element |
| `panic` | `String -> 'a` | Raises a runtime error |
