#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "core/ast.h"
#include "core/errors.h"
#include "parser.tab.h"

/* Returns a heap-allocated copy that must be freed by the caller. */
char *duplicate_token_text(const char *source);
/* Returns a heap-allocated copy that must be freed by the caller. */
char *duplicate_substring(const char *source, size_t length);
/* Returns a heap-allocated unquoted copy that must be freed by the caller. */
char *duplicate_quoted_text(const char *source, size_t length);
void lexer_set_error(TclError *error);
void lexer_set_line(int line);
void lexer_reset_state(void);
#endif
