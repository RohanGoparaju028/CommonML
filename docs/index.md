---
layout: default
title: CommonML Documentation
---

# CommonML Documentation

Welcome to the official documentation for **CommonML** (`cml` and `cmlc`), a statically typed functional systems programming language with Hindley-Milner type inference, explicit heap lifetimes (`new`, `close`, `defer`), pointers (`*T`, `->`), algebraic data types, hygienic AST macros, a package/build system (`build.cml`), and an interactive source debugger (`cml debug` / DAP).

## Start learning CommonML

<div class="cards">
  <div class="card">
    <h3><a href="tour-of-commonml.html">A tour of CommonML</a></h3>
    <p>Walk through types, structs, pointers, defer cleanup, functions, and pattern matching in 10 minutes.</p>
  </div>
  <div class="card">
    <h3><a href="getting-started/installation.html">Installers &amp; Setup</a></h3>
    <p>Install native <code>.msi</code>, <code>.pkg</code>, or <code>.deb</code> packages so <code>cml</code> and <code>cmlc</code> run directly from any terminal.</p>
  </div>
  <div class="card">
    <h3><a href="getting-started/hello-world.html">Hello World &amp; build.cml</a></h3>
    <p>Compile standalone executables with <code>cmlc</code>, run scripts with <code>cml run</code>, and scaffold projects with <code>build.cml</code>.</p>
  </div>
</div>

## Language fundamentals

<div class="cards">
  <div class="card">
    <h3><a href="fundamentals/types.html">Types &amp; Data Structures</a></h3>
    <p>Fixed-width integers (<code>i8</code>–<code>u64</code>, <code>usize</code>), floats (<code>f32</code>, <code>f64</code>), <code>bool</code>, <code>str</code>/<code>String</code>, <code>struct</code>, ADTs, <code>Vector</code>, <code>Dict</code>, and <code>Set</code>.</p>
  </div>
  <div class="card">
    <h3><a href="fundamentals/memory-and-pointers.html">Memory Safety &amp; Pointers</a></h3>
    <p>Heap allocations with <code>new</code>, deterministic freeing with <code>close</code> and <code>defer</code>, pointer access via <code>-></code> and <code>*</code>, and runtime use-after-free checks.</p>
  </div>
  <div class="card">
    <h3><a href="fundamentals/control-flow-and-functions.html">Functions &amp; Control Flow</a></h3>
    <p>Uniform <code>=</code> and <code>:</code> indentation blocks, <code>let f = func(...) -&gt; Ret:</code>, pattern matching, <code>while</code>, <code>for</code>, and <code>for each</code> loops.</p>
  </div>
</div>

## Reference & Tooling

- **[Modules & build.cml Packages](fundamentals/modules-and-packages.html)**: Assignment-based `let m = import "..."` and dependency management.
- **[Hygienic Macros](macros.html)**: Compile-time AST metaprogramming with `quote { ... }` and `$` splicing.
- **[Standard Library & Builtins](reference/stdlib.html)**: `std/core`, `std/math`, `std/console`, `std/net`, `std/http`, file I/O, and heap/RC diagnostics.
- **[CLI, Debugger (DAP) & IDEs](tooling/cli-editors-debugger.html)**: Interactive debugging, VS Code / Vim / Neovim / Emacs support, and OS `.cml` file recognition.
- **[Compiler Architecture & Self-Hosting](architecture.html)**: Bytecode format (`CMLBC 1`), stack VM, and the `bootstrap/` compiler moving to CommonML + LLVM IR.
