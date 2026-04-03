#include <stdlib.h>

#include "parser.h"
#include "lexer.h"

typedef void *YY_BUFFER_STATE;

YY_BUFFER_STATE yy_scan_string(const char *yy_str);
void yy_delete_buffer(YY_BUFFER_STATE buffer_state);

int main(void)
{
    char line[4096];

    while (fgets(line, sizeof(line), stdin))
    {
        char *interpolated_line;
        YY_BUFFER_STATE buffer_state;

        if (line[0] == '\n' || line[0] == '\r')
        {
            continue;
        }

        interpolated_line = interpolate_input_text(line);
        if (!interpolated_line)
        {
            fprintf(stderr, "failed to interpolate input: %s", line);
            return EXIT_FAILURE;
        }

        lexer_reset_state();
        buffer_state = yy_scan_string(interpolated_line);
        if (!buffer_state)
        {
            free(interpolated_line);
            fprintf(stderr, "failed to initialize lexer buffer\n");
            return EXIT_FAILURE;
        }

        if (yyparse() != 0)
        {
            yy_delete_buffer(buffer_state);
            free(interpolated_line);
            return EXIT_FAILURE;
        }

        yy_delete_buffer(buffer_state);
        free(interpolated_line);
    }

    return 0;
}