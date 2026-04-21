#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/validator/internal.h"

static int parse_list_item(const char *text, size_t *index, char **item)
{
    size_t length = strlen(text);
    size_t start;

    while (*index < length && isspace((unsigned char)text[*index]))
    {
        (*index)++;
    }

    if (*index >= length)
    {
        *item = NULL;
        return 1;
    }

    if (text[*index] == '{')
    {
        int depth = 1;
        start = ++(*index);
        while (*index < length)
        {
            if (text[*index] == '{')
            {
                depth++;
            }
            else if (text[*index] == '}')
            {
                depth--;
                if (depth == 0)
                {
                    *item = validator_copy_substring(text + start, *index - start);
                    (*index)++;
                    return *item != NULL;
                }
            }
            (*index)++;
        }
        return 0;
    }

    start = *index;
    while (*index < length && !isspace((unsigned char)text[*index]))
    {
        (*index)++;
    }

    *item = validator_copy_substring(text + start, *index - start);
    return *item != NULL;
}

static int split_arg_spec(const char *item, char **name, int *has_default)
{
    size_t index = 0;
    char *first;
    char *second;

    if (!parse_list_item(item, &index, &first) || !first)
    {
        return 0;
    }

    if (!parse_list_item(item, &index, &second))
    {
        free(first);
        return 0;
    }

    while (item[index] && isspace((unsigned char)item[index]))
    {
        index++;
    }

    if (item[index] != '\0')
    {
        free(first);
        free(second);
        return 0;
    }

    *name = first;
    *has_default = second != NULL;
    free(second);
    return 1;
}

static int parse_proc_args(
    const AstWord *args_word,
    size_t *required_count,
    size_t *max_count,
    int *variadic,
    TclError *error)
{
    size_t index = 0;
    char *item = NULL;
    int saw_variadic = 0;

    if (!validator_word_is_literal_script(args_word))
    {
        return validator_syntax_error_at_word(error, args_word, "proc args must be a literal list");
    }

    *required_count = 0;
    *max_count = 0;
    *variadic = 0;

    while (1)
    {
        char *name = NULL;
        int has_default = 0;

        if (!parse_list_item(args_word->text, &index, &item))
        {
            free(item);
            return validator_syntax_error_at_word(error, args_word, "invalid proc argument list");
        }

        if (!item)
        {
            break;
        }

        if (!split_arg_spec(item, &name, &has_default) || name[0] == '\0')
        {
            free(item);
            free(name);
            return validator_syntax_error_at_word(error, args_word, "invalid proc argument list");
        }

        if (saw_variadic)
        {
            free(item);
            free(name);
            return validator_syntax_error_at_word(error, args_word, "args must be the last proc argument");
        }

        if (strcmp(name, "args") == 0)
        {
            saw_variadic = 1;
            *variadic = 1;
        }
        else
        {
            if (!has_default)
            {
                (*required_count)++;
            }
            (*max_count)++;
        }

        free(item);
        free(name);
        item = NULL;
    }

    return 1;
}

int validator_collect_proc_definitions(ValidatorContext *context, const AstCommand *program, TclError *error)
{
    const AstCommand *command = program;

    while (command)
    {
        int count = validator_word_count(command);
        const AstWord *name_word;
        const AstWord *args_word;
        size_t required_count;
        size_t max_count;
        int variadic;

        if (count == 4 && validator_word_is_literal_keyword(command->words, "proc"))
        {
            name_word = validator_word_at(command, 1);
            args_word = validator_word_at(command, 2);

            if (!validator_word_is_literal_name(name_word))
            {
                return validator_syntax_error_at_word(error, name_word, "proc name must be literal");
            }

            if (!parse_proc_args(args_word, &required_count, &max_count, &variadic, error))
            {
                return 0;
            }

            if (!validator_add_or_update_procedure(
                    context,
                    name_word->text,
                    required_count,
                    max_count,
                    variadic,
                    error,
                    name_word))
            {
                return 0;
            }
        }

        command = command->next;
    }

    return 1;
}
