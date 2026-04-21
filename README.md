# tcl-int

Tcl parser/executor project in C (flex + bison -y).

## Current Scope

This repository currently supports a focused command subset:

- `set <name> <value>`
- `put <text>`
- `puts <text>`
- `puts -nonewline <text>`
- `put $<name>`
- `put {<literal text>}` (including nested braces)
- `if ... elseif ... else ...`
- `while <test> <body>`
- `for <start> <test> <next> <body>`
- `proc <name> <args> <body>`
- `expr <expression>`
- `return ?value?`
- `break`
- `continue`
- validation-only `incr`, `list`, `foreach`, and `switch`
- calls to procedures declared with `proc`

Control/procedure bodies are validated recursively, but they are not executed in this stage.
Unknown literal command names are rejected unless they refer to a declared procedure.

The implementation also supports word substitution for the current top-level executable subset, including quoted strings and embedded variable references like `x$a` and `${a}`.
Command substitutions (`[...]`) are structurally validated as nested scripts. Their values are not evaluated yet; top-level execution keeps a placeholder/literal word value for this stage.

Current command syntax behavior:

- Semicolon (`;`) and newline are command separators.
- `#` starts a comment only when Tcl expects the first word of a command.
- `#` inside a word is treated as a regular character.
- Backslash-newline continuations are normalized before parsing.
- Array-style variable references such as `$a(x)` are accepted and stored by their literal variable name.
- `puts` supports only `puts <text>` and `puts -nonewline <text>`; channel output (`puts <channelId> <text>`) is not supported.

## Architecture

- Lexer: tokenization and source location tracking.
- Parser: builds an AST for the full input script.
- Validator: recursively checks supported command forms, procedure arity, command substitutions, and expressions.
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
