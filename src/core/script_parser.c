#include <stdlib.h>
#include <string.h>

#include "core/script_parser.h"
#include "lexer.h"
#include "parser.h"

typedef void *YY_BUFFER_STATE;

YY_BUFFER_STATE yy_scan_string(const char *yy_str);
void yy_delete_buffer(YY_BUFFER_STATE buffer_state);

static int is_horizontal_space(char character)
{
    return character == ' ' || character == '\t';
}

static char *preprocess_script(const char *script)
{
    size_t length = strlen(script);
    char *result = (char *)malloc(length + 2);
    size_t read_index = 0;
    size_t write_index = 0;

    if (!result)
    {
        return NULL;
    }

    while (read_index < length)
    {
        if (script[read_index] == '\\' &&
            read_index + 1 < length &&
            (script[read_index + 1] == '\n' || script[read_index + 1] == '\r'))
        {
            /*
             * Tcl backslash-newline continuation:
             * replace '\' + newline + following horizontal spaces with a single space.
             */
            read_index++;
            if (script[read_index] == '\r' &&
                read_index + 1 < length &&
                script[read_index + 1] == '\n')
            {
                read_index++;
            }
            read_index++;

            while (read_index < length && is_horizontal_space(script[read_index]))
            {
                read_index++;
            }

            result[write_index++] = ' ';
            continue;
        }

        result[write_index++] = script[read_index++];
    }

    result[write_index++] = '\n';
    result[write_index] = '\0';
    return result;
}

AstCommand *parse_script(const char *script, int start_line, int start_column, TclError *error)
{
    YY_BUFFER_STATE buffer_state;
    AstCommand *program;
    char *preprocessed;

    lexer_reset_state();
    lexer_set_position(start_line, start_column);
    parser_begin(error);

    preprocessed = preprocess_script(script);
    if (!preprocessed)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, start_line, start_column, "out of memory");
        return NULL;
    }

    buffer_state = yy_scan_string(preprocessed);
    if (!buffer_state)
    {
        free(preprocessed);
        tcl_error_set(error, TCL_ERROR_SYSTEM, start_line, start_column, "failed to initialize lexer buffer");
        return NULL;
    }

    if (yyparse() != 0)
    {
        program = parser_take_program();
        ast_command_free(program);
        yy_delete_buffer(buffer_state);
        free(preprocessed);
        return NULL;
    }

    program = parser_take_program();
    yy_delete_buffer(buffer_state);
    free(preprocessed);
    return program;
}
