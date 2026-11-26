---
layout: default
title: CLI, Debugger and Editors
---

# CLI, Debugger (`cml debug` / DAP) & IDE Setup

## CLI Commands (`cml` & `cmlc`)

```sh
cml run hello.cml            # Run directly
cmlc hello.cml               # Compile to ./hello
./hello                      # Execute compiled output
cml debug hello.cml -b 12    # Interactive source-level debugger
cml --highlight hello.cml    # ANSI syntax-highlighted source output
cml --bc hello.cml           # Disassemble bytecode
cml --check hello.cml        # Typecheck without executing
```

## Interactive Debugger (`cml debug`)

| Command | Action |
| --- | --- |
| `b <line>` / `d <line>` | Set or delete breakpoint at source line |
| `c`, `n`, `s`, `o` | Continue, step over (`next`), step in (`step`), step out |
| `locals` | Inspect all live variables in the current call frame |
| `p <var \| p->field \| *p>` | Evaluate variable, struct field, or pointer dereference |
| `heap` | Show live unclosed `new` heap allocation count |
| `bt`, `l`, `q` | Backtrace, list source lines, quit |

## Debug Adapter Protocol (`cml --dap`)

`cml --dap` exposes a JSON-RPC Debug Adapter Protocol server over stdio supporting `launch`, `setBreakpoints`, `stackTrace`, `scopes`, `variables`, `evaluate`, `continue`, `next`, `stepIn`, `stepOut`, and `disconnect`.

## Editor & OS Integration

- **VS Code**: [editors/vscode/](https://github.com/RohanGoparaju028/CommonML/tree/main/editors/vscode) provides `.cml` / `build.cml` syntax highlighting (`tmlanguage`) and `commonml` launch debugging via `cml --dap`.
- **Vim / Neovim**: [editors/vim/](https://github.com/RohanGoparaju028/CommonML/tree/main/editors/vim) provides `ftdetect/cml.vim`, `syntax/cml.vim`, `indent/cml.vim`, and `plugin/commonml-dap.lua`.
- **Emacs**: [editors/emacs/cml-mode.el](https://github.com/RohanGoparaju028/CommonML/blob/main/editors/emacs/cml-mode.el) registers `cml-mode` in `auto-mode-alist` for `*.cml`.
- **OS File Types**:
  - Linux: XDG MIME definition `packaging/linux/commonml.xml` (`text/x-commonml`) + `.desktop` launcher.
  - macOS: UTI `org.commonml.source` in `packaging/macos/Info.plist`.
  - Windows: `packaging/windows/register_cml.cmd` associates `.cml` and `.cmlbc`.
- **GitHub Recognition**: `.gitattributes` marks `*.cml linguist-detectable=true` and `packaging/linguist/commonml-linguist.yml` holds the draft definition for `github-linguist/linguist`.
