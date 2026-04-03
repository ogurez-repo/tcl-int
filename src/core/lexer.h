#ifndef LEXER_H
#define LEXER_H

#include <stdlib.h>
#include <string.h>

#include "parser.tab.h"

char *duplicate_token_text(char *source);
char *interpolate_token_text(char *source);
char *interpolate_input_text(char *source);
void lexer_reset_state(void);
#endif