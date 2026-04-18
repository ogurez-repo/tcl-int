#ifndef ERRORS_H
#define ERRORS_H

#include <stdarg.h>

typedef enum TclErrorType
{
    TCL_ERROR_NONE,
    TCL_ERROR_LEXICAL,
    TCL_ERROR_SYNTAX,
    TCL_ERROR_SEMANTIC,
    TCL_ERROR_SYSTEM
} TclErrorType;

typedef struct TclError
{
    TclErrorType type;
    int line;
    int column;
    char message[256];
} TclError;

/* Resets error to no-error state. */
void tcl_error_clear(TclError *error);
/* Stores first error only; subsequent calls are ignored until clear. */
void tcl_error_set(TclError *error, TclErrorType type, int line, int column, const char *message);
/* Stores first error only; subsequent calls are ignored until clear. */
void tcl_error_setf(TclError *error, TclErrorType type, int line, int column, const char *format, ...);
const char *tcl_error_type_name(TclErrorType type);

#endif
