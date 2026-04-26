#include <stdlib.h>
#include <string.h>

#include "runtime/validator/internal.h"

static int is_var_start_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_var_char(char c)
{
    return is_var_start_char(c) || (c >= '0' && c <= '9');
}

static int is_supported_name(const char *name)
{
    size_t index;

    if (!name || name[0] == '\0' || !is_var_start_char(name[0]))
    {
        return 0;
    }

    for (index = 1; name[index]; index++)
    {
        if (!is_var_char(name[index]))
        {
            return 0;
        }
    }

    return 1;
}

static int parse_proc_args(const AstWord *args_word, size_t *arg_count, TclError *error)
{
    size_t index = 0;
    size_t count = 0;

    if (!validator_word_is_literal_script(args_word))
    {
        return validator_syntax_error_at_word(error, args_word, "proc args must be a literal list");
    }

    while (1)
    {
        char *item = NULL;

        if (!validator_parse_list_item(args_word->text, &index, &item))
        {
            free(item);
            return validator_syntax_error_at_word(error, args_word, "invalid proc argument list");
        }

        if (!item)
        {
            break;
        }

        if (!is_supported_name(item) || strcmp(item, "args") == 0)
        {
            free(item);
            return validator_syntax_error_at_word(error, args_word, "invalid proc argument list");
        }

        free(item);
        count++;
    }

    *arg_count = count;
    return 1;
}

int validator_collect_proc_definitions(ValidatorContext *context, const AstCommand *program, TclError *error)
{
    const AstCommand *command = program;

    while (command)
    {
        int count = validator_word_count(command);

        if (count == 4 && validator_word_is_literal_keyword(command->words, "proc"))
        {
            const AstWord *name_word = validator_word_at(command, 1);
            const AstWord *args_word = validator_word_at(command, 2);
            size_t arg_count = 0;

            if (!validator_word_is_literal_name(name_word) || !is_supported_name(name_word->text))
            {
                return validator_syntax_error_at_word(error, name_word, "proc name must be literal");
            }

            if (!parse_proc_args(args_word, &arg_count, error))
            {
                return 0;
            }

            if (!validator_add_or_update_procedure(context, name_word->text, arg_count, error, name_word))
            {
                return 0;
            }
        }

        command = command->next;
    }

    return 1;
}
