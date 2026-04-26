%{
#include <stdlib.h>

#include "parser.h"
#include "core/ast.h"
#include "core/errors.h"
#include "lexer.h"

static AstCommand *s_program_head = NULL;
static AstCommand *s_program_tail = NULL;
static TclError *s_error = NULL;
%}

%locations

%union {
    char *str;
    AstWord *word;
}

%token <str> STRING
%token <str> QUOTED
%token <str> BRACED
%token <str> VAR
%token <str> VAR_BRACED
%token CMDSEP
%token INVALID

%type <word> word words

%%

program:
            /* empty */
        | separators
        | separators command_list separators_opt
        | command_list separators_opt
    ;

command_list:
      command
    | command_list separators command
    ;

separators_opt:
      /* empty */
    | separators
    ;

separators:
      CMDSEP
    | separators CMDSEP
    ;

command:
    words {
        SourceSpan span;
        AstCommand *command;
        AstWord *last_word = $1;

        while (last_word->next)
        {
            last_word = last_word->next;
        }

        span.line = $1->span.line;
        span.column = $1->span.column;
        span.end_line = last_word->span.end_line;
        span.end_column = last_word->span.end_column;
        command = ast_command_create($1, &span);
        if (!command)
        {
            ast_word_free($1);
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, span.line, span.column, "out of memory");
            YYERROR;
        }
        ast_command_append(&s_program_head, &s_program_tail, command);
    }
    ;

words:
    word {
        $$ = $1;
    }
    | words word {
        ast_word_append($1, $2);
        $$ = $1;
    }
    ;

word:
    STRING {
        SourceSpan span;

        span.line = @1.first_line;
        span.column = @1.first_column;
        span.end_line = @1.last_line;
        span.end_column = @1.last_column;

        $$ = ast_word_create(AST_WORD_STRING, $1, &span);
        if (!$$)
        {
            free($1);
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, span.line, span.column, "out of memory");
            YYERROR;
        }
    }
    | QUOTED {
        SourceSpan span;

        span.line = @1.first_line;
        span.column = @1.first_column;
        span.end_line = @1.last_line;
        span.end_column = @1.last_column;

        $$ = ast_word_create(AST_WORD_QUOTED, $1, &span);
        if (!$$)
        {
            free($1);
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, span.line, span.column, "out of memory");
            YYERROR;
        }
    }
    | BRACED {
        SourceSpan span;

        span.line = @1.first_line;
        span.column = @1.first_column;
        span.end_line = @1.last_line;
        span.end_column = @1.last_column;

        $$ = ast_word_create(AST_WORD_BRACED, $1, &span);
        if (!$$)
        {
            free($1);
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, span.line, span.column, "out of memory");
            YYERROR;
        }
    }
    | VAR {
        SourceSpan span;

        span.line = @1.first_line;
        span.column = @1.first_column;
        span.end_line = @1.last_line;
        span.end_column = @1.last_column;

        $$ = ast_word_create(AST_WORD_VAR, $1, &span);
        if (!$$)
        {
            free($1);
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, span.line, span.column, "out of memory");
            YYERROR;
        }
    }
    | VAR_BRACED {
        SourceSpan span;

        span.line = @1.first_line;
        span.column = @1.first_column;
        span.end_line = @1.last_line;
        span.end_column = @1.last_column;

        $$ = ast_word_create(AST_WORD_VAR_BRACED, $1, &span);
        if (!$$)
        {
            free($1);
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, span.line, span.column, "out of memory");
            YYERROR;
        }
    }
    | INVALID {
        if (s_error && s_error->type == TCL_ERROR_NONE)
        {
            tcl_error_set(s_error, TCL_ERROR_LEXICAL, @1.first_line, @1.first_column, "invalid token");
        }

        YYERROR;
    }
    ;
%%

void yyerror(const char *message)
{
    extern YYLTYPE yylloc;
    extern int yychar;

    TclErrorType type = TCL_ERROR_SYNTAX;

    if (yychar == INVALID)
    {
        type = TCL_ERROR_LEXICAL;
    }

    if (s_error && s_error->type == TCL_ERROR_NONE)
    {
        tcl_error_set(s_error, type, yylloc.first_line, yylloc.first_column, message);
    }
}

void parser_begin(TclError *error)
{
    parser_discard_program();
    s_error = error;
    lexer_set_error(error);
}

AstCommand *parser_take_program(void)
{
    AstCommand *program = s_program_head;
    s_program_head = NULL;
    s_program_tail = NULL;
    return program;
}

void parser_discard_program(void)
{
    if (!s_program_head)
    {
        return;
    }

    ast_command_free(s_program_head);
    s_program_head = NULL;
    s_program_tail = NULL;
}
