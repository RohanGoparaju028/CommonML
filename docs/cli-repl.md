---
layout: default
title: CLI and REPL Reference
---

# CLI & REPL Guide

## CLI Usage

```text
commonml [options] [file.cml]
```

Options:
- `-e <expr>`: Evaluate inline CommonML source and print the resulting value
- `--tokens`: Print lexer tokens
- `--ast`: Print parsed AST
- `--expanded`: Print macro-expanded AST
- `--types`: Print inferred types for top-level statements
- `--bc`: Disassemble compiled bytecode
- `--check`: Run lexing, parsing, macro expansion, and typechecking without executing
- `-h`, `--help`: Show help message

Running `commonml` with no arguments starts the interactive REPL.

## REPL Commands

| Command | Action |
| --- | --- |
| `:help`, `:h` | Show available commands |
| `:quit`, `:q` | Exit the REPL |
| `:type <expr>` | Infer and print the type of an expression without executing |
| `:ast` | Toggle AST dump for each evaluation |
| `:bc` | Toggle bytecode disassembly for each evaluation |
| `:reset` | Reset REPL environment and state |

End a line with `\` to continue entering multi-line input.
