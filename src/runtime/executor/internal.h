#ifndef EXECUTOR_INTERNAL_H
#define EXECUTOR_INTERNAL_H

#include "runtime/executor.h"
#include "core/ast.h"
#include "core/errors.h"

/* Utility: append text to a growing buffer. */
int append_text(char **buffer, size_t *capacity, size_t *length, const char *text, size_t text_length);

/* Variable name scanning (also used by expr evaluator). */
size_t scan_variable_name(const char *text, size_t length, size_t start);

/* Utility: free array of heap-allocated strings. */
void free_values(char **values, size_t count);

/* Word evaluation: resolve substitutions (variables, backslash, command-subst) in text. */
int resolve_string_text(
    ExecutorContext *context,
    const char *source_text,
    const SourceSpan *span,
    TclError *error,
    char **result);

/* Evaluate a single AstWord into a heap-allocated string. */
int evaluate_word(ExecutorContext *context, const AstWord *word, TclError *error, char **result);

/* Evaluate an expansion word ({*}) into a list of strings. */
int evaluate_expansion_word(
    ExecutorContext *context,
    const AstWord *word,
    TclError *error,
    char ***items,
    size_t *item_count);

/* Evaluate a linked list of AstWords into an array of strings. */
int evaluate_words(
    ExecutorContext *context,
    const AstWord *head,
    size_t count,
    TclError *error,
    char ***values,
    size_t *value_count);

/* Execute a script inside […] and return its result string. */
int execute_command_substitution(
    ExecutorContext *context,
    const char *text,
    size_t length,
    size_t start,
    int base_line,
    int base_column,
    size_t *end,
    TclError *error,
    char **result);

/* Evaluate a Tcl expr string and return its result string. */
int evaluate_expression(
    ExecutorContext *context,
    const char *text,
    int line,
    int column,
    TclError *error,
    char **result);

/* Parse a string as a script and execute it. */
int execute_script_text(
    ExecutorContext *context,
    const char *text,
    int line,
    int column,
    TclError *error);

/* Parse a string as a script and execute it with full control-flow propagation. */
ExecCode execute_script_text_with_code(
    ExecutorContext *context,
    const char *text,
    int line,
    int column,
    TclError *error,
    char **result);

/* Execute an AST program and return both control-flow code and script result. */
ExecCode executor_execute_program(
    ExecutorContext *context,
    const AstCommand *program,
    TclError *error,
    char **result);

/* Parse one item from a Tcl-formatted list string. */
int parse_list_item(const char *text, size_t *index, char **item);

/* Convert a decimal integer string to long long. */
int expr_to_longlong(const char *str, long long *out);

#endif
