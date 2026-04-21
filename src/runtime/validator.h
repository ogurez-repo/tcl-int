#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "core/ast.h"
#include "core/errors.h"

typedef struct ValidatorContext ValidatorContext;

ValidatorContext *validator_create(void);
void validator_destroy(ValidatorContext *context);

/* Validates the supported Tcl subset without executing command bodies. */
int validator_validate_program(ValidatorContext *context, const AstCommand *program, TclError *error);
int validator_has_procedure(const ValidatorContext *context, const char *name);

/* Shared if-validation helper used by both validator and executor. */
int validator_validate_if_words(ValidatorContext *context, const AstWord **words, int count, TclError *error);

#endif
