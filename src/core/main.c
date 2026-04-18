#include <stdlib.h>

#include "core/ast.h"
#include "core/errors.h"
#include "runtime/executor.h"
#include "parser.h"
#include "lexer.h"

typedef void *YY_BUFFER_STATE;

YY_BUFFER_STATE yy_scan_string(const char *yy_str);
void yy_delete_buffer(YY_BUFFER_STATE buffer_state);

int main(void)
{
    char line[4096];
    int line_number = 0;
    ExecutorContext *context;

    context = executor_create(stdout, stderr);
    if (!context)
    {
        fprintf(stderr, "failed to initialize executor\n");
        return EXIT_FAILURE;
    }

    while (fgets(line, sizeof(line), stdin))
    {
        TclError error;
        AstCommand *program;
        YY_BUFFER_STATE buffer_state;

        line_number++;

        if (line[0] == '\n' || line[0] == '\r')
        {
            continue;
        }

        tcl_error_clear(&error);
        lexer_reset_state();
        lexer_set_line(line_number);
        parser_begin(&error);

        buffer_state = yy_scan_string(line);
        if (!buffer_state)
        {
            fprintf(stderr, "failed to initialize lexer buffer\n");
            executor_destroy(context);
            return EXIT_FAILURE;
        }

        if (yyparse() != 0)
        {
            program = parser_take_program();
            ast_command_free(program);
            yy_delete_buffer(buffer_state);

            if (error.type != TCL_ERROR_NONE)
            {
                fprintf(
                    stderr,
                    "%s error at %d:%d: %s\n",
                    tcl_error_type_name(error.type),
                    error.line,
                    error.column,
                    error.message);
            }

            executor_destroy(context);
            return EXIT_FAILURE;
        }

        program = parser_take_program();
        if (!executor_execute(context, program, &error))
        {
            ast_command_free(program);
            yy_delete_buffer(buffer_state);

            fprintf(
                stderr,
                "%s error at %d:%d: %s\n",
                tcl_error_type_name(error.type),
                error.line,
                error.column,
                error.message);
            executor_destroy(context);
            return EXIT_FAILURE;
        }

        ast_command_free(program);

        yy_delete_buffer(buffer_state);
    }

    executor_destroy(context);
    return 0;
}
