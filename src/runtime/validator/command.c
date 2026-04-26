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

static int is_supported_var_name(const char *name)
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

static int is_expand_word_type(AstWordType type)
{
    return type == AST_WORD_EXPAND_EMPTY ||
           type == AST_WORD_EXPAND_STRING ||
           type == AST_WORD_EXPAND_QUOTED ||
           type == AST_WORD_EXPAND_BRACED ||
           type == AST_WORD_EXPAND_VAR ||
           type == AST_WORD_EXPAND_VAR_BRACED;
}

static int validate_word_substitutions(ValidatorContext *context, const AstWord *word, TclError *error)
{
    (void)context;

    if (is_expand_word_type(word->type))
    {
        return validator_syntax_error_at_word(error, word, "argument expansion is not supported");
    }

    if (word->type == AST_WORD_VAR || word->type == AST_WORD_VAR_BRACED)
    {
        if (!is_supported_var_name(word->text))
        {
            return validator_syntax_error_at_word(error, word, "array/namespace variables are not supported");
        }
        return 1;
    }

    if (word->type == AST_WORD_STRING || word->type == AST_WORD_QUOTED)
    {
        size_t index = 0;
        size_t length = strlen(word->text);

        if (!validator_validate_command_substitutions_in_text(
            context,
            word->text,
            word->span.line,
            word->span.column,
            error))
        {
            return 0;
        }

        while (index < length)
        {
            if (word->text[index] == '\\' && index + 1 < length)
            {
                index += 2;
                continue;
            }

            if (word->text[index] != '$')
            {
                index++;
                continue;
            }

            index++;
            if (index >= length)
            {
                break;
            }

            if (word->text[index] == '{')
            {
                size_t start;
                index++;
                start = index;
                while (index < length && word->text[index] != '}')
                {
                    index++;
                }

                if (index >= length)
                {
                    return validator_syntax_error_at_word(error, word, "unterminated braced variable reference");
                }

                {
                    char *var_name = validator_copy_substring(word->text + start, index - start);
                    int valid;
                    if (!var_name)
                    {
                        tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
                        return 0;
                    }

                    valid = is_supported_var_name(var_name);
                    free(var_name);
                    if (!valid)
                    {
                        return validator_syntax_error_at_word(error, word, "array/namespace variables are not supported");
                    }
                }

                index++;
                continue;
            }

            if (word->text[index] == ':')
            {
                return validator_syntax_error_at_word(error, word, "array/namespace variables are not supported");
            }

            if (!is_var_start_char(word->text[index]))
            {
                continue;
            }

            index++;
            while (index < length && is_var_char(word->text[index]))
            {
                index++;
            }

            if (index < length && word->text[index] == '(')
            {
                return validator_syntax_error_at_word(error, word, "array/namespace variables are not supported");
            }

            if (index + 1 < length && word->text[index] == ':' && word->text[index + 1] == ':')
            {
                return validator_syntax_error_at_word(error, word, "array/namespace variables are not supported");
            }
        }

        return 1;
    }

    return 1;
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
    if (word->type == AST_WORD_VAR || word->type == AST_WORD_VAR_BRACED)
    {
        if (!is_supported_var_name(word->text))
        {
            return validator_syntax_error_at_word(error, word, "array/namespace variables are not supported");
        }
        return 1;
    }

    if (!validator_word_is_literal_script(word))
    {
        return validator_syntax_error_at_word(error, word, "expression must be literal");
    }

    return validator_validate_expression_text(context, word->text, word->span.line, word->span.column, error);
}

static int validate_if_command(ValidatorContext *context, const AstCommand *command, int count, TclError *error)
{
    const AstWord **words = (const AstWord **)malloc(sizeof(const AstWord *) * (size_t)count);
    int index;
    int result;

    if (!words)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
        return 0;
    }

    for (index = 0; index < count; index++)
    {
        words[index] = validator_word_at(command, index);
    }

    result = validator_validate_if_words(context, words, count, error);
    free(words);
    return result;
}

static int validate_proc_call(const Procedure *procedure, int arg_count, TclError *error, const AstCommand *command)
{
    if ((size_t)arg_count != procedure->arg_count)
    {
        tcl_error_setf(
            error,
            TCL_ERROR_SYNTAX,
            command->span.line,
            command->span.column,
            "procedure expects %zu arguments",
            procedure->arg_count);
        return 0;
    }

    return 1;
}

static int validate_expr_command(ValidatorContext *context, const AstCommand *command, int count, TclError *error)
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

    {
        size_t total_length = 0;
        size_t offset = 0;
        int index;
        char *buffer;
        const AstWord *first_word = validator_word_at(command, 1);
        int result;

        for (index = 1; index < count; index++)
        {
            total_length += strlen(validator_word_at(command, index)->text);
            if (index < count - 1)
            {
                total_length++;
            }
        }

        buffer = (char *)malloc(total_length + 1);
        if (!buffer)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
            return 0;
        }

        for (index = 1; index < count; index++)
        {
            const AstWord *word = validator_word_at(command, index);
            size_t length = strlen(word->text);
            memcpy(buffer + offset, word->text, length);
            offset += length;
            if (index < count - 1)
            {
                buffer[offset++] = ' ';
            }
        }
        buffer[offset] = '\0';

        result = validator_validate_expression_text(context, buffer, first_word->span.line, first_word->span.column, error);
        free(buffer);
        return result;
    }
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
        const AstWord *var_name;

        if (count != 2 && count != 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "set expects 1 or 2 arguments");
            return 0;
        }

        var_name = validator_word_at(command, 1);
        if (validator_word_is_literal_name(var_name) && !is_supported_var_name(var_name->text))
        {
            return validator_syntax_error_at_word(error, var_name, "array/namespace variables are not supported");
        }
        return 1;
    }

    if (strcmp(name_word->text, "unset") == 0)
    {
        const AstWord *var_name;

        if (count != 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "unset expects exactly 1 argument");
            return 0;
        }

        var_name = validator_word_at(command, 1);
        if (validator_word_is_literal_name(var_name) && !is_supported_var_name(var_name->text))
        {
            return validator_syntax_error_at_word(error, var_name, "array/namespace variables are not supported");
        }
        return 1;
    }

    if (strcmp(name_word->text, "incr") == 0)
    {
        const AstWord *var_name;

        if (count != 2 && count != 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "incr expects 1 or 2 arguments");
            return 0;
        }

        var_name = validator_word_at(command, 1);
        if (validator_word_is_literal_name(var_name) && !is_supported_var_name(var_name->text))
        {
            return validator_syntax_error_at_word(error, var_name, "array/namespace variables are not supported");
        }
        return 1;
    }

    if (strcmp(name_word->text, "puts") == 0)
    {
        if (count == 2)
        {
            return 1;
        }

        if (count == 3)
        {
            const AstWord *arg1 = validator_word_at(command, 1);
            if (validator_word_is_literal_keyword(arg1, "-nonewline") ||
                validator_word_is_literal_keyword(arg1, "stdout") ||
                validator_word_is_literal_keyword(arg1, "stderr"))
            {
                return 1;
            }
        }

        if (count == 4)
        {
            const AstWord *arg1 = validator_word_at(command, 1);
            const AstWord *arg2 = validator_word_at(command, 2);
            if (validator_word_is_literal_keyword(arg1, "-nonewline") &&
                (validator_word_is_literal_keyword(arg2, "stdout") || validator_word_is_literal_keyword(arg2, "stderr")))
            {
                return 1;
            }
        }

        tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "puts expects string or -nonewline string");
        return 0;
    }

    if (strcmp(name_word->text, "gets") == 0)
    {
        if (count != 2 && count != 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "gets expects stdin ?varName?");
            return 0;
        }

        if (validator_word_is_literal_name(validator_word_at(command, 1)) &&
            !validator_word_is_literal_keyword(validator_word_at(command, 1), "stdin"))
        {
            return validator_syntax_error_at_word(error, validator_word_at(command, 1), "gets supports only stdin");
        }

        if (count == 3 &&
            validator_word_is_literal_name(validator_word_at(command, 2)) &&
            !is_supported_var_name(validator_word_at(command, 2)->text))
        {
            return validator_syntax_error_at_word(error, validator_word_at(command, 2), "array/namespace variables are not supported");
        }

        return 1;
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

    if (strcmp(name_word->text, "break") == 0)
    {
        if (count != 1)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "break expects no arguments");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "continue") == 0)
    {
        if (count != 1)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "continue expects no arguments");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "return") == 0)
    {
        if (count != 1 && count != 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "return expects ?value?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "proc") == 0)
    {
        const AstWord *proc_name;

        if (count != 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "proc expects exactly 3 arguments");
            return 0;
        }

        proc_name = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(proc_name) || !is_supported_var_name(proc_name->text))
        {
            return validator_syntax_error_at_word(error, proc_name, "proc name must be literal");
        }

        return validate_script_word(context, validator_word_at(command, 3), error);
    }

    if (strcmp(name_word->text, "expr") == 0)
    {
        return validate_expr_command(context, command, count, error);
    }

    procedure = validator_find_procedure(context, name_word->text);
    if (procedure)
    {
        return validate_proc_call(procedure, count - 1, error, command);
    }

    tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown command '%s'", name_word->text);
    return 0;
}
