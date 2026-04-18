#ifndef AST_H
#define AST_H

typedef struct SourceSpan
{
    int line;
    int column;
    int end_column;
} SourceSpan;

typedef enum AstWordType
{
    AST_WORD_STRING,
    AST_WORD_BRACED,
    AST_WORD_VAR
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
