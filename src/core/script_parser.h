#ifndef SCRIPT_PARSER_H
#define SCRIPT_PARSER_H

#include "core/ast.h"
#include "core/errors.h"

/* Parses script text into an AST command list owned by the caller. */
AstCommand *parse_script(const char *script, int start_line, int start_column, TclError *error);

#endif
