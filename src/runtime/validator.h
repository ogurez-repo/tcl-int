#ifndef VALIDATOR_H
#define VALIDATOR_H

#include <stddef.h>

#include "core/ast.h"
#include "core/errors.h"

typedef struct ValidatorContext ValidatorContext;

ValidatorContext *validator_create(void);
void validator_destroy(ValidatorContext *context);

/* Validates the supported Tcl subset without executing command bodies. */
int validator_validate_program(ValidatorContext *context, const AstCommand *program, TclError *error);
int validator_has_procedure(const ValidatorContext *context, const char *name);
int validator_declare_procedure(
    ValidatorContext *context,
    const char *name,
    size_t required_count,
    size_t max_count,
    int variadic,
    TclError *error,
    int line,
    int column);

/* Shared if-validation helper used by both validator and executor. */
int validator_validate_if_words(ValidatorContext *context, const AstWord **words, int count, TclError *error);

/* Find the matching ']' for a '[' starting at `start`. */
int validator_find_matching_bracket(const char *text, size_t start, size_t *end);

#endif
