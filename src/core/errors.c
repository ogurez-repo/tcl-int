#include <stdio.h>
#include <string.h>

#include "core/errors.h"

void tcl_error_clear(TclError *error)
{
    if (!error)
    {
        return;
    }

    error->type = TCL_ERROR_NONE;
    error->line = 0;
    error->column = 0;
    error->message[0] = '\0';
}

void tcl_error_set(TclError *error, TclErrorType type, int line, int column, const char *message)
{
    if (!error || error->type != TCL_ERROR_NONE)
    {
        return;
    }

    error->type = type;
    error->line = line;
    error->column = column;
    snprintf(error->message, sizeof(error->message), "%s", message);
}

void tcl_error_setf(TclError *error, TclErrorType type, int line, int column, const char *format, ...)
{
    va_list args;

    if (!error || error->type != TCL_ERROR_NONE)
    {
        return;
    }

    error->type = type;
    error->line = line;
    error->column = column;

    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
}

const char *tcl_error_type_name(TclErrorType type)
{
    switch (type)
    {
        case TCL_ERROR_LEXICAL:
            return "lexical";
        case TCL_ERROR_SYNTAX:
            return "syntax";
        case TCL_ERROR_SEMANTIC:
            return "semantic";
        case TCL_ERROR_SYSTEM:
            return "system";
        case TCL_ERROR_NONE:
        default:
            return "none";
    }
}
