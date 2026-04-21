#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/validator/internal.h"

static int validate_word_substitutions(ValidatorContext *context, const AstWord *word, TclError *error)
{
    if (word->type != AST_WORD_STRING)
    {
        return 1;
    }

    return validator_validate_command_substitutions_in_text(
        context,
        word->text,
        word->span.line,
        word->span.column,
        error);
}

static int validate_words_substitutions(ValidatorContext *context, const AstCommand *command, TclError *error)
{
    const AstWord *word = command->words;

    while (word)
    {
        if (!validate_word_substitutions(context, word, error))
        {
            return 0;
        }
        word = word->next;
    }

    return 1;
}

static int validate_script_word(ValidatorContext *context, const AstWord *word, TclError *error)
{
    if (!validator_word_is_literal_script(word))
    {
        return validator_syntax_error_at_word(error, word, "script body must be literal");
    }

    return validator_validate_script_text(context, word->text, word->span.line, word->span.column + 1, error);
}

static int validate_expr_word(ValidatorContext *context, const AstWord *word, TclError *error)
{
    if (word->type == AST_WORD_VAR)
    {
        return 1;
    }

    if (!validator_word_is_literal_script(word))
    {
        return validator_syntax_error_at_word(error, word, "expression must be literal");
    }

    return validator_validate_expression_text(context, word->text, word->span.line, word->span.column, error);
}

static int validate_proc_call(const Procedure *procedure, int arg_count, TclError *error, const AstCommand *command)
{
    if ((size_t)arg_count < procedure->required_count)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "procedure expects more arguments");
        return 0;
    }

    if (!procedure->variadic && (size_t)arg_count > procedure->max_count)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "procedure expects fewer arguments");
        return 0;
    }

    return 1;
}

static int validate_if_command(ValidatorContext *context, const AstCommand *command, int count, TclError *error)
{
    const AstWord **words;
    int i;
    int result;

    words = (const AstWord **)malloc(sizeof(const AstWord *) * count);
    if (!words)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
        return 0;
    }

    for (i = 0; i < count; i++)
    {
        words[i] = validator_word_at(command, i);
    }

    result = validator_validate_if_words(context, words, count, error);
    free(words);
    return result;
}

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

static int validate_foreach_command(ValidatorContext *context, const AstCommand *command, int count, TclError *error)
{
    if (count < 4 || ((count - 2) % 2) != 0)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "foreach expects var/list pairs and body");
        return 0;
    }

    return validate_script_word(context, validator_word_at(command, count - 1), error);
}

static int validate_switch_body_text(
    ValidatorContext *context,
    const char *body,
    const AstWord *source,
    TclError *error)
{
    if (strcmp(body, "-") == 0)
    {
        return 1;
    }

    return validator_validate_script_text(context, body, source->span.line, source->span.column + 1, error);
}

static int validate_switch_braced_cases(
    ValidatorContext *context,
    const AstWord *cases_word,
    TclError *error)
{
    size_t index = 0;
    int item_count = 0;

    while (1)
    {
        char *pattern = NULL;
        char *body = NULL;

        if (!parse_list_item(cases_word->text, &index, &pattern))
        {
            free(pattern);
            return validator_syntax_error_at_word(error, cases_word, "invalid switch case list");
        }

        if (!pattern)
        {
            break;
        }

        if (!parse_list_item(cases_word->text, &index, &body) || !body)
        {
            free(pattern);
            free(body);
            return validator_syntax_error_at_word(error, cases_word, "switch expects pattern/body pairs");
        }

        item_count += 2;
        if (!validate_switch_body_text(context, body, cases_word, error))
        {
            free(pattern);
            free(body);
            return 0;
        }

        free(pattern);
        free(body);
    }

    if (item_count == 0 || (item_count % 2) != 0)
    {
        return validator_syntax_error_at_word(error, cases_word, "switch expects pattern/body pairs");
    }

    return 1;
}

static int validate_switch_command(ValidatorContext *context, const AstCommand *command, int count, TclError *error)
{
    int index = 1;

    if (index < count && validator_word_is_literal_keyword(validator_word_at(command, index), "--"))
    {
        index++;
    }

    if (count - index < 2)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "switch expects string and cases");
        return 0;
    }

    index++;

    if (count - index == 1)
    {
        const AstWord *cases_word = validator_word_at(command, index);
        if (!validator_word_is_literal_script(cases_word))
        {
            return validator_syntax_error_at_word(error, cases_word, "switch case list must be literal");
        }
        return validate_switch_braced_cases(context, cases_word, error);
    }

    if (((count - index) % 2) != 0)
    {
        return validator_syntax_error_at_word(error, validator_word_at(command, index), "switch expects pattern/body pairs");
    }

    while (index < count)
    {
        const AstWord *body = validator_word_at(command, index + 1);

        if (validator_word_is_literal_keyword(body, "-"))
        {
            index += 2;
            continue;
        }

        if (!validate_script_word(context, body, error))
        {
            return 0;
        }

        index += 2;
    }

    return 1;
}

int validator_validate_command(ValidatorContext *context, const AstCommand *command, TclError *error)
{
    int count = validator_word_count(command);
    const AstWord *name_word = command->words;
    Procedure *procedure;

    if (count == 0)
    {
        return 1;
    }

    if (!validate_words_substitutions(context, command, error))
    {
        return 0;
    }

    if (!validator_word_is_literal_name(name_word))
    {
        return 1;
    }

    if (strcmp(name_word->text, "set") == 0)
    {
        if (count != 2 && count != 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "set expects 1 or 2 arguments");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "put") == 0)
    {
        if (count != 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "put expects exactly 1 argument");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "puts") == 0)
    {
        if (count == 2)
        {
            return 1;
        }

        if (count == 3 && validator_word_is_literal_keyword(validator_word_at(command, 1), "-nonewline"))
        {
            return 1;
        }

        tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "puts expects string or -nonewline string");
        return 0;
    }

    if (strcmp(name_word->text, "if") == 0)
    {
        return validate_if_command(context, command, count, error);
    }

    if (strcmp(name_word->text, "while") == 0)
    {
        if (count != 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "while expects exactly 2 arguments");
            return 0;
        }
        return validate_expr_word(context, validator_word_at(command, 1), error) &&
               validate_script_word(context, validator_word_at(command, 2), error);
    }

    if (strcmp(name_word->text, "for") == 0)
    {
        if (count != 5)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "for expects exactly 4 arguments");
            return 0;
        }
        return validate_script_word(context, validator_word_at(command, 1), error) &&
               validate_expr_word(context, validator_word_at(command, 2), error) &&
               validate_script_word(context, validator_word_at(command, 3), error) &&
               validate_script_word(context, validator_word_at(command, 4), error);
    }

    if (strcmp(name_word->text, "proc") == 0)
    {
        if (count != 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "proc expects exactly 3 arguments");
            return 0;
        }
        return validate_script_word(context, validator_word_at(command, 3), error);
    }

    if (strcmp(name_word->text, "expr") == 0)
    {
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "expr expects at least 1 argument");
            return 0;
        }

        if (count == 2)
        {
            return validate_expr_word(context, validator_word_at(command, 1), error);
        }

        /* Concatenate multiple expr arguments with spaces, matching Tcl semantics. */
        {
            size_t total_length = 0;
            int i;
            for (i = 1; i < count; i++)
            {
                total_length += strlen(validator_word_at(command, i)->text);
                if (i < count - 1)
                {
                    total_length += 1;
                }
            }

            char *buffer = (char *)malloc(total_length + 1);
            if (!buffer)
            {
                tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
                return 0;
            }

            size_t offset = 0;
            for (i = 1; i < count; i++)
            {
                const AstWord *word = validator_word_at(command, i);
                size_t len = strlen(word->text);
                memcpy(buffer + offset, word->text, len);
                offset += len;
                if (i < count - 1)
                {
                    buffer[offset++] = ' ';
                }
            }
            buffer[offset] = '\0';

            const AstWord *first_word = validator_word_at(command, 1);
            int result = validator_validate_expression_text(context, buffer, first_word->span.line, first_word->span.column, error);
            free(buffer);
            return result;
        }
    }

    if (strcmp(name_word->text, "incr") == 0)
    {
        if (count != 2 && count != 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "incr expects 1 or 2 arguments");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "list") == 0)
    {
        return 1;
    }

    if (strcmp(name_word->text, "foreach") == 0)
    {
        return validate_foreach_command(context, command, count, error);
    }

    if (strcmp(name_word->text, "switch") == 0)
    {
        return validate_switch_command(context, command, count, error);
    }

    if (strcmp(name_word->text, "return") == 0)
    {
        if (count == 1 || count == 2)
        {
            return 1;
        }

        /* Tcl: return ?-option value ...? ?result?
         * If there are 3+ args, the first extra arg must start with '-'. */
        {
            const AstWord *first_arg = validator_word_at(command, 1);
            if (!first_arg || first_arg->text[0] != '-')
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "return expects ?-option value ...? ?result?");
                return 0;
            }
        }

        return 1;
    }

    if (strcmp(name_word->text, "catch") == 0)
    {
        if (count != 2 && count != 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "catch expects script ?varName?");
            return 0;
        }
        return validate_script_word(context, validator_word_at(command, 1), error);
    }

    if (strcmp(name_word->text, "break") == 0 || strcmp(name_word->text, "continue") == 0)
    {
        if (count != 1)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "loop control expects no arguments");
            return 0;
        }
        return 1;
    }

    procedure = validator_find_procedure(context, name_word->text);
    if (procedure)
    {
        return validate_proc_call(procedure, count - 1, error, command);
    }

    tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown command '%s'", name_word->text);
    return 0;
}
