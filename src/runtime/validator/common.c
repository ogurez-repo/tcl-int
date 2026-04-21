#include <stdlib.h>
#include <string.h>

#include "runtime/validator/internal.h"

char *validator_copy_substring(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1);
    if (!copy)
    {
        return NULL;
    }

    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

char *validator_copy_string(const char *text)
{
    return validator_copy_substring(text, strlen(text));
}

void validator_advance_position(char character, int *line, int *column)
{
    if (character == '\n')
    {
        (*line)++;
        *column = 1;
        return;
    }

    (*column)++;
}

int validator_word_count(const AstCommand *command)
{
    int count = 0;
    const AstWord *word = command->words;

    while (word)
    {
        count++;
        word = word->next;
    }

    return count;
}

const AstWord *validator_word_at(const AstCommand *command, int index)
{
    const AstWord *word = command->words;
    int current = 0;

    while (word && current < index)
    {
        word = word->next;
        current++;
    }

    return word;
}

static int word_has_substitution(const AstWord *word)
{
    return word->type == AST_WORD_STRING &&
           (strchr(word->text, '$') || strchr(word->text, '['));
}

int validator_word_is_literal_name(const AstWord *word)
{
    if (!word)
    {
        return 0;
    }

    if (word->type == AST_WORD_BRACED)
    {
        return 1;
    }

    if (word->type != AST_WORD_STRING)
    {
        return 0;
    }

    return !word_has_substitution(word);
}

int validator_word_is_literal_keyword(const AstWord *word, const char *keyword)
{
    return validator_word_is_literal_name(word) && strcmp(word->text, keyword) == 0;
}

int validator_word_is_literal_script(const AstWord *word)
{
    return word && (word->type == AST_WORD_BRACED || word->type == AST_WORD_STRING);
}

int validator_syntax_error_at_word(TclError *error, const AstWord *word, const char *message)
{
    tcl_error_set(error, TCL_ERROR_SYNTAX, word->span.line, word->span.column, message);
    return 0;
}
