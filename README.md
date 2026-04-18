# tcl-int

Tcl parser/executor project in C (flex + bison -y).

## Current Scope

This repository currently supports a focused command subset:

- `set <name> <value>`
- `put <text>`
- `put $<name>`
- `put {<literal text>}` (including nested braces)

The parser also validates structural syntax for:

- `if ... elseif ... else ...`
- `while <test> <body>`
- `for <start> <test> <next> <body>`
- `proc <name> <args> <body>`

These control/procedure forms are currently syntax-validated only (their bodies are not executed in this stage).

The implementation also supports word substitution for the current subset, including quoted strings and embedded variable references like `x$a` and `${a}`.

Current command syntax behavior:

- Semicolon (`;`) and newline are command separators.
- `#` starts a comment only when Tcl expects the first word of a command.
- `#` inside a word is treated as a regular character.

## Architecture

- Lexer: tokenization and source location tracking.
- Parser: builds an AST for the full input script.
- Executor: evaluates AST words and dispatches supported commands.
- Runtime store: variable storage with explicit lifecycle APIs.
- Error model: typed diagnostics (`lexical`, `syntax`, `semantic`, `system`) with line/column.

## Diagnostics

Error output format is:

```text
<type> error at <line>:<column>: <message>
```

Where type is one of:

- `lexical`
- `syntax`
- `semantic`
- `system`

## Build

Requirements:

- `gcc`
- `flex`
- `bison`
- `make`
- `pytest`

Platform notes:

- macOS/Linux: run directly in a POSIX shell.
- Windows: use a POSIX-compatible shell environment (for example MSYS2 or Git Bash) so `make`, `bison`, and `flex` work as expected.

Build:

```bash
make
```

## Run

```bash
make run
```

or directly:

```bash
./bin/tclsh
```

## Test

```bash
make test
```
