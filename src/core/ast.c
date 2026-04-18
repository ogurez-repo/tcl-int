#include <stdlib.h>

#include "core/ast.h"

AstWord *ast_word_create(AstWordType type, char *text, const SourceSpan *span)
{
    AstWord *word = (AstWord *)malloc(sizeof(AstWord));
    if (!word)
    {
        return NULL;
    }

    word->type = type;
    word->text = text;
    word->span = *span;
    word->next = NULL;

    return word;
}

void ast_word_append(AstWord *head, AstWord *tail)
{
    AstWord *cursor = head;

    while (cursor->next)
    {
        cursor = cursor->next;
    }

    cursor->next = tail;
}

void ast_word_free(AstWord *head)
{
    while (head)
    {
        AstWord *next = head->next;
        free(head->text);
        free(head);
        head = next;
    }
}

AstCommand *ast_command_create(AstWord *words, const SourceSpan *span)
{
    AstCommand *command = (AstCommand *)malloc(sizeof(AstCommand));
    if (!command)
    {
        return NULL;
    }

    command->words = words;
    command->span = *span;
    command->next = NULL;

    return command;
}

void ast_command_append(AstCommand **head, AstCommand **tail, AstCommand *command)
{
    if (!*head)
    {
        *head = command;
        *tail = command;
        return;
    }

    (*tail)->next = command;
    *tail = command;
}

void ast_command_free(AstCommand *head)
{
    while (head)
    {
        AstCommand *next = head->next;
        ast_word_free(head->words);
        free(head);
        head = next;
    }
}
