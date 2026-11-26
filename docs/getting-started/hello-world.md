---
layout: default
title: Hello World and build.cml
---

# Hello World & `build.cml` Projects

Once installed, you can run single `.cml` files, compile standalone executables with `cmlc`, or manage multi-file packages with `build.cml`.

## Single-file workflow

Create `hello.cml`:

```text
let greet name = "Hello, " ++ name ++ "!"
println (greet "CommonML")
```

Run immediately (like `go run`):

```sh
cml run hello.cml
```

Or compile to a standalone bytecode executable:

```sh
cmlc hello.cml
./hello
```

## Project workflow (`build.cml`)

Create a new project:

```sh
cml new console my_app
cd my_app
```

This creates a Zig-inspired `build.cml` manifest in your project root alongside `src/main.cml` and `tests/main_test.cml`:

```sh
cml build
cml run
cml test
```
