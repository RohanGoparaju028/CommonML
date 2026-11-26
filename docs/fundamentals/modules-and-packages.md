---
layout: default
title: Modules and build.cml Packages
---

# Modules & Packages (`build.cml`)

## Assignment Imports

Every module import is an explicit `let` binding returning a module namespace accessed via `.`:

```text
let math = import "std/math"
let console = import "std/console"
let helpers = import "src/helpers.cml"

let v = math.sqrt 16.0
```

Imports are deduplicated per compilation and cyclic imports are statically rejected.

## `build.cml` Package Configuration

CommonML projects use a Zig-inspired `build.cml` file in the repository root (no `.toml` manifests required):

```sh
cml new console my_app
cml add math_utils --path ../math_utils
cml install
cml build
cml run -- arg1 arg2
cml test
cml pkg list
```
