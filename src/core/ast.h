#ifndef AST_H
#define AST_H

typedef struct SourceSpan
{
    int line;
    int column;
    int end_line;
    int end_column;
} SourceSpan;

typedef enum AstWordType
{
    AST_WORD_STRING,
    AST_WORD_QUOTED,
    AST_WORD_BRACED,
    AST_WORD_VAR,
    AST_WORD_VAR_BRACED,
    AST_WORD_EXPAND_EMPTY,
    AST_WORD_EXPAND_STRING,
    AST_WORD_EXPAND_QUOTED,
    AST_WORD_EXPAND_BRACED,
    AST_WORD_EXPAND_VAR,
    AST_WORD_EXPAND_VAR_BRACED
} AstWordType;

typedef struct AstWord
{
    AstWordType type;
    char *text;
    SourceSpan span;
    struct AstWord *next;
} AstWord;

typedef struct AstCommand
{
    AstWord *words;
    SourceSpan span;
    struct AstCommand *next;
} AstCommand;

/* Takes ownership of text on success. */
AstWord *ast_word_create(AstWordType type, char *text, const SourceSpan *span);
void ast_word_append(AstWord *head, AstWord *tail);
/* Frees node list and owned word texts. */
void ast_word_free(AstWord *head);

/* Takes ownership of words on success. */
AstCommand *ast_command_create(AstWord *words, const SourceSpan *span);
void ast_command_append(AstCommand **head, AstCommand **tail, AstCommand *command);
/* Frees command list and all nested words. */
void ast_command_free(AstCommand *head);

#endif
