# MiniTcl Interpreter

Tcl subset interpreter implemented in C with `flex` + `bison`.

### Supported command model

- Commands are separated by newline or `;`
- Words:
  - bare words
  - quoted words (`"..."`)
  - braced words (`{...}`)
  - command substitution (`[...]`)
- Variable substitution:
  - `$name`
  - `${name}`
- Minimal backslash escapes in runtime word resolution:
  - `\n`, `\t`, `\\`, `\"`, `\{`, `\}`, `\[`, `\]`, `\$`, `\;`

### Supported built-in commands

- `set`
- `unset`
- `incr`
- `expr`
- `puts`
- `gets` (only `stdin`)
- `if` / `elseif` / `else` (`then` as optional noise word)
- `while`
- `for`
- `break`
- `continue`
- `proc`
- `return`

### Procedure and control-flow behavior

- Fixed-arity procedures only
- No default arguments and no `args` variadic parameter
- `return` performs proper unwind from procedure body
- `break` / `continue` are handled by loops
- `break` / `continue` outside loops are runtime errors
- `return` outside procedures is a runtime error
- Internal execution model uses result codes:
  - `OK`, `ERROR`, `RETURN`, `BREAK`, `CONTINUE`

### Expression subset (`expr`)

Supported operators:

- Arithmetic: `+ - * / %`
- Comparison: `== != < <= > >=`
- Logical: `! && ||`
- Parentheses and unary `+ - !`

Constraints:

- Decimal integers only
- Integer division
- Division/modulo by zero is a runtime error

## Not Supported (by design)

- Argument expansion `{*}`
- Arrays and namespace-qualified variables
- `switch`, `foreach`, `catch`, `list`
- Tcl list/dict/array subsystems
- `global`, `upvar`, `namespace`, `eval`, `source`, `uplevel`
- Floating-point and prefixed numeric literals in `expr` (`0x`, `0b`, `0o`, `3.14`, etc.)
- Extended `expr` operators/functions (`**`, bitwise operators, ternary, math functions)

## Diagnostics

Error format:

```text
<type> error at <line>:<column>: <message>
```

Where `<type>` is one of:

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

Build:

```bash
make
```

## Run

```bash
./bin/tclsh [--run|--check] [script_file]
```

Behavior:

- If `script_file` is provided, script is read from file.
- If `script_file` is omitted, script is read from `stdin` (compat mode).
- In file mode, `stdin` remains available to script commands like `gets stdin`.
- `--check` performs parser/validator checks without runtime execution.
- `--run` (default) executes the script.

Examples:

```bash
# Execute from file
./bin/tclsh program.tcl

# Validate only
./bin/tclsh --check program.tcl

# Script from stdin (compat mode)
printf 'puts ok\n' | ./bin/tclsh
```

## Tests

Separated suites:

- Parser/Lexer-focused: `tests/parser_lexer`
- Runtime-focused: `tests/runtime`

Commands:

```bash
make test-parser-lexer
make test-runtime
make test
```
