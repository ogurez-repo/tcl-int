#ifndef VALIDATOR_INTERNAL_H
#define VALIDATOR_INTERNAL_H

#include <stddef.h>

#include "core/ast.h"
#include "core/errors.h"
#include "runtime/validator.h"

typedef struct Procedure
{
    char *name;
    size_t required_count;
    size_t max_count;
    int variadic;
    struct Procedure *next;
} Procedure;

struct ValidatorContext
{
    Procedure *procedures;
};

char *validator_copy_substring(const char *text, size_t length);
char *validator_copy_string(const char *text);
void validator_advance_position(char character, int *line, int *column);

int validator_word_count(const AstCommand *command);
const AstWord *validator_word_at(const AstCommand *command, int index);
int validator_word_is_literal_name(const AstWord *word);
int validator_word_is_literal_keyword(const AstWord *word, const char *keyword);
int validator_word_is_literal_script(const AstWord *word);
int validator_syntax_error_at_word(TclError *error, const AstWord *word, const char *message);
int validator_parse_list_item(const char *text, size_t *index, char **item);

Procedure *validator_find_procedure(ValidatorContext *context, const char *name);
int validator_add_or_update_procedure(
    ValidatorContext *context,
    const char *name,
    size_t required_count,
    size_t max_count,
    int variadic,
    TclError *error,
    const AstWord *source);

int validator_collect_proc_definitions(ValidatorContext *context, const AstCommand *program, TclError *error);

int validator_find_matching_bracket(const char *text, size_t start, size_t *end);
int validator_validate_command_substitutions_in_text(
    ValidatorContext *context,
    const char *text,
    int start_line,
    int start_column,
    TclError *error);
int validator_sanitize_script_text(
    ValidatorContext *context,
    const char *script,
    int start_line,
    int start_column,
    TclError *error,
    char **sanitized);
int validator_validate_script_text(
    ValidatorContext *context,
    const char *script,
    int start_line,
    int start_column,
    TclError *error);

int validator_validate_expression_text(
    ValidatorContext *context,
    const char *text,
    int line,
    int column,
    TclError *error);

int validator_validate_command(ValidatorContext *context, const AstCommand *command, TclError *error);

#endif
