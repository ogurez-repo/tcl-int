#include <stdlib.h>
#include <string.h>

#include "runtime/validator/internal.h"

static int validate_word_substitutions(ValidatorContext *context, const AstWord *word, TclError *error)
{
    if (word->type == AST_WORD_VAR || word->type == AST_WORD_EXPAND_VAR)
    {
        const char *open_paren = strchr(word->text, '(');
        const char *close_paren;
        char *index_text;
        int result;

        if (!open_paren)
        {
            return 1;
        }

        close_paren = strrchr(word->text, ')');
        if (!close_paren || close_paren < open_paren)
        {
            return validator_syntax_error_at_word(error, word, "unterminated array variable reference");
        }

        index_text = validator_copy_substring(open_paren + 1, (size_t)(close_paren - open_paren - 1));
        if (!index_text)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }

        result = validator_validate_command_substitutions_in_text(
            context,
            index_text,
            word->span.line,
            word->span.column,
            error);
        free(index_text);
        return result;
    }

    if (word->type == AST_WORD_STRING ||
        word->type == AST_WORD_QUOTED ||
        word->type == AST_WORD_EXPAND_STRING ||
        word->type == AST_WORD_EXPAND_QUOTED)
    {
        return validator_validate_command_substitutions_in_text(
            context,
            word->text,
            word->span.line,
            word->span.column,
            error);
    }

    if (word->type == AST_WORD_EXPAND_VAR_BRACED ||
        word->type == AST_WORD_EXPAND_EMPTY ||
        word->type == AST_WORD_EXPAND_BRACED)
    {
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
        return 1;
    }

    return validator_validate_script_text(context, word->text, word->span.line, word->span.column + 1, error);
}

static int validate_expr_word(ValidatorContext *context, const AstWord *word, TclError *error)
{
    int result;

    if (word->type == AST_WORD_VAR || word->type == AST_WORD_VAR_BRACED)
    {
        return 1;
    }

    if (!validator_word_is_literal_script(word))
    {
        return validator_syntax_error_at_word(error, word, "expression must be literal");
    }

    result = validator_validate_expression_text(context, word->text, word->span.line, word->span.column, error);
    if (!result && word->type != AST_WORD_BRACED &&
        (strchr(word->text, '$') || strchr(word->text, '[') || strchr(word->text, '\\')))
    {
        /* Unquoted/quoted expression contains substitutions or escapes that may become valid
         * at runtime. We can't statically validate them, so we accept them to avoid false
         * negatives. Braced expressions are literal, so any syntax error there is real. */
        tcl_error_clear(error);
        return 1;
    }
    return result;
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

static int validate_foreach_varlist_word(const AstWord *varlist_word, TclError *error)
{
    size_t index = 0;
    size_t item_count = 0;

    if (!validator_word_is_literal_script(varlist_word))
    {
        return 1;
    }

    while (1)
    {
        char *item = NULL;

        if (!validator_parse_list_item(varlist_word->text, &index, &item))
        {
            free(item);
            return validator_syntax_error_at_word(error, varlist_word, "invalid foreach variable list");
        }

        if (!item)
        {
            break;
        }

        item_count++;
        free(item);
    }

    if (item_count == 0)
    {
        return validator_syntax_error_at_word(error, varlist_word, "foreach varlist is empty");
    }

    return 1;
}

static int validate_foreach_command(ValidatorContext *context, const AstCommand *command, int count, TclError *error)
{
    int index;

    if (count < 4 || ((count - 2) % 2) != 0)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "foreach expects var/list pairs and body");
        return 0;
    }

    for (index = 1; index < count - 1; index += 2)
    {
        if (!validate_foreach_varlist_word(validator_word_at(command, index), error))
        {
            return 0;
        }
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
    int needs_following_body = 0;

    while (1)
    {
        char *pattern = NULL;
        char *body = NULL;

        if (!validator_parse_list_item(cases_word->text, &index, &pattern))
        {
            free(pattern);
            return validator_syntax_error_at_word(error, cases_word, "invalid switch case list");
        }

        if (!pattern)
        {
            break;
        }

        if (!validator_parse_list_item(cases_word->text, &index, &body) || !body)
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

        needs_following_body = (strcmp(body, "-") == 0);
        free(pattern);
        free(body);
    }

    if (item_count == 0 || (item_count % 2) != 0)
    {
        return validator_syntax_error_at_word(error, cases_word, "switch expects pattern/body pairs");
    }

    if (needs_following_body)
    {
        return validator_syntax_error_at_word(error, cases_word, "switch pattern has no body");
    }

    return 1;
}

static int validate_switch_command(ValidatorContext *context, const AstCommand *command, int count, TclError *error)
{
    int index = 1;
    int remaining_args = count - 1;
    int saw_regexp = 0;
    int matchvar_option_index = -1;
    int indexvar_option_index = -1;

    /*
     * Tcl special case: with exactly two switch arguments (string + cases),
     * a leading '-' word is treated as the string, not as an option.
     */
    if (remaining_args != 2)
    {
        while (index < count)
        {
            const AstWord *option = validator_word_at(command, index);

            if (!validator_word_is_literal_name(option))
            {
                break;
            }

            if (strcmp(option->text, "--") == 0)
            {
                index++;
                break;
            }

            if (strcmp(option->text, "-exact") == 0 ||
                strcmp(option->text, "-glob") == 0 ||
                strcmp(option->text, "-nocase") == 0)
            {
                index++;
                continue;
            }

            if (strcmp(option->text, "-regexp") == 0)
            {
                saw_regexp = 1;
                index++;
                continue;
            }

            if (strcmp(option->text, "-matchvar") == 0 ||
                strcmp(option->text, "-indexvar") == 0)
            {
                const AstWord *variable_name;

                if (index + 1 >= count)
                {
                    return validator_syntax_error_at_word(error, option, "switch option expects a variable name");
                }

                variable_name = validator_word_at(command, index + 1);
                if (!validator_word_is_literal_name(variable_name))
                {
                    return validator_syntax_error_at_word(error, option, "switch option expects a variable name");
                }

                if (strcmp(option->text, "-matchvar") == 0 && matchvar_option_index < 0)
                {
                    matchvar_option_index = index;
                }
                if (strcmp(option->text, "-indexvar") == 0 && indexvar_option_index < 0)
                {
                    indexvar_option_index = index;
                }

                index += 2;
                continue;
            }

            if (option->text[0] == '-')
            {
                return validator_syntax_error_at_word(error, option, "unknown switch option");
            }

            break;
        }
    }

    if (!saw_regexp && (matchvar_option_index >= 0 || indexvar_option_index >= 0))
    {
        if (matchvar_option_index >= 0 &&
            (indexvar_option_index < 0 || matchvar_option_index < indexvar_option_index))
        {
            return validator_syntax_error_at_word(
                error,
                validator_word_at(command, matchvar_option_index),
                "-matchvar option requires -regexp option");
        }

        return validator_syntax_error_at_word(
            error,
            validator_word_at(command, indexvar_option_index),
            "-indexvar option requires -regexp option");
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
        if (validator_word_is_literal_script(cases_word))
        {
            return validate_switch_braced_cases(context, cases_word, error);
        }
        return 1;
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
            if (index + 2 >= count)
            {
                return validator_syntax_error_at_word(error, body, "switch pattern has no body");
            }
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

    if (strcmp(name_word->text, "cmdsubst") == 0)
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
        if (count >= 2 && count <= 4)
        {
            return 1;
        }

        tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "puts expects ?channelId? string or -nonewline ?channelId? string");
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

        /* Concatenate multiple expr arguments with spaces, matching Tcl semantics.
         * Reconstruct $var and ${var} prefixes so the expression parser sees them. */
        {
            size_t total_length = 0;
            int i;
            for (i = 1; i < count; i++)
            {
                const AstWord *word = validator_word_at(command, i);
                size_t len = strlen(word->text);
                size_t prefix = 0;
                size_t suffix = 0;
                if (word->type == AST_WORD_VAR)
                {
                    prefix = 1; /* $ */
                }
                else if (word->type == AST_WORD_VAR_BRACED)
                {
                    prefix = 2; /* ${ */
                    suffix = 1; /* } */
                }
                total_length += prefix + len + suffix;
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
                if (word->type == AST_WORD_VAR)
                {
                    buffer[offset++] = '$';
                }
                else if (word->type == AST_WORD_VAR_BRACED)
                {
                    buffer[offset++] = '$';
                    buffer[offset++] = '{';
                }
                memcpy(buffer + offset, word->text, len);
                offset += len;
                if (word->type == AST_WORD_VAR_BRACED)
                {
                    buffer[offset++] = '}';
                }
                if (i < count - 1)
                {
                    buffer[offset++] = ' ';
                }
            }
            buffer[offset] = '\0';

            const AstWord *first_word = validator_word_at(command, 1);
            int result = validator_validate_expression_text(context, buffer, first_word->span.line, first_word->span.column, error);
            if (result)
            {
                free(buffer);
                return 1;
            }
            /* If concatenation fails, try validating each word as a valid primary expression.
             * This handles cases like `expr {[pop]} $op {$t}` where $op contains an operator. */
            tcl_error_clear(error);
            for (i = 1; i < count; i++)
            {
                if (!validate_expr_word(context, validator_word_at(command, i), error))
                {
                    free(buffer);
                    return 0;
                }
            }
            free(buffer);
            return 1;
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

    if (strcmp(name_word->text, "unset") == 0)
    {
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "unset expects at least 1 argument");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "array") == 0)
    {
        const AstWord *subcommand_word;

        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "array expects a subcommand");
            return 0;
        }

        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }

        if (strcmp(subcommand_word->text, "size") == 0 ||
            strcmp(subcommand_word->text, "exists") == 0)
        {
            if (count != 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "array %s expects arrayName", subcommand_word->text);
                return 0;
            }
            return 1;
        }

        if (strcmp(subcommand_word->text, "names") == 0 ||
            strcmp(subcommand_word->text, "get") == 0 ||
            strcmp(subcommand_word->text, "unset") == 0)
        {
            if (count != 3 && count != 4)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "array %s expects arrayName ?pattern?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "startsearch") == 0 ||
            strcmp(subcommand_word->text, "anymore") == 0 ||
            strcmp(subcommand_word->text, "nextelement") == 0 ||
            strcmp(subcommand_word->text, "donesearch") == 0)
        {
            if (count < 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "array %s expects arguments", subcommand_word->text);
                return 0;
            }
            return 1;
        }

        if (strcmp(subcommand_word->text, "set") == 0)
        {
            if (count != 4)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "array set expects arrayName list");
                return 0;
            }
            return 1;
        }

        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown array subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "append") == 0 || strcmp(name_word->text, "lappend") == 0)
    {
        if (count < 3)
        {
            tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "%s expects varName ?value ...?", name_word->text);
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "lindex") == 0)
    {
        if (count < 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "lindex expects list ?index ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "llength") == 0)
    {
        if (count != 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "llength expects list");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "concat") == 0)
    {
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "concat expects ?arg ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "lrange") == 0)
    {
        if (count != 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "lrange expects list first last");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "linsert") == 0)
    {
        if (count < 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "linsert expects list index element ?element ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "lreplace") == 0)
    {
        if (count < 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "lreplace expects list first last ?element ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "split") == 0 || strcmp(name_word->text, "join") == 0)
    {
        if (count != 2 && count != 3)
        {
            tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "%s expects string/list ?chars/joinString?", name_word->text);
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "format") == 0)
    {
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "format expects formatString ?arg ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "scan") == 0)
    {
        if (count < 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "scan expects string format ?varName ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "error") == 0)
    {
        if (count < 2 || count > 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "error expects message ?info? ?code?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "eval") == 0)
    {
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "eval expects arg ?arg ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "global") == 0)
    {
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "global expects varName ?varName ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "upvar") == 0)
    {
        int index = 1;
        if (count < 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "upvar expects ?level? otherVar myVar ?...?");
            return 0;
        }
        if (count >= 4)
        {
            const AstWord *level_word = validator_word_at(command, 1);
            if (validator_word_is_literal_name(level_word))
            {
                const char *level_text = level_word->text;
                if ((level_text[0] >= '0' && level_text[0] <= '9') || level_text[0] == '#')
                {
                    index = 2;
                }
            }
        }
        if ((count - index) % 2 != 0)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "upvar expects pairs of variable names");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "regexp") == 0)
    {
        int index = 1;
        while (index < count)
        {
            const AstWord *option = validator_word_at(command, index);
            if (!validator_word_is_literal_name(option) || option->text[0] != '-')
                break;
            if (strcmp(option->text, "-start") == 0)
            {
                if (index + 1 >= count)
                {
                    return validator_syntax_error_at_word(error, option, "regexp -start expects an index");
                }
                index += 2;
                continue;
            }
            index++;
            continue;
        }
        if (count - index < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "regexp expects expression and string");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "regsub") == 0)
    {
        int index = 1;
        while (index < count)
        {
            const AstWord *option = validator_word_at(command, index);
            if (!validator_word_is_literal_name(option) || option->text[0] != '-')
                break;
            if (strcmp(option->text, "-start") == 0)
            {
                if (index + 1 >= count)
                {
                    return validator_syntax_error_at_word(error, option, "regsub -start expects an index");
                }
                index += 2;
                continue;
            }
            index++;
            continue;
        }
        if (count - index < 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "regsub expects expression string subSpec ?varName?");
            return 0;
        }
        if (count - index > 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "regsub expects too many arguments");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "string") == 0)
    {
        const AstWord *subcommand_word;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "length") == 0 ||
            strcmp(subcommand_word->text, "tolower") == 0 ||
            strcmp(subcommand_word->text, "toupper") == 0)
        {
            if (count != 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string %s expects a string", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "index") == 0)
        {
            if (count != 4)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string index expects string charIndex");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "range") == 0)
        {
            if (count != 5)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string range expects string first last");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "match") == 0)
        {
            if (count != 4)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string match expects pattern string");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "compare") == 0 ||
            strcmp(subcommand_word->text, "equal") == 0)
        {
            if (count != 4)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string %s expects two strings", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "first") == 0 ||
            strcmp(subcommand_word->text, "last") == 0)
        {
            if (count != 4 && count != 5)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string %s expects needleString haystackString ?startIndex?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "trim") == 0 ||
            strcmp(subcommand_word->text, "trimleft") == 0 ||
            strcmp(subcommand_word->text, "trimright") == 0)
        {
            if (count != 3 && count != 4)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string %s expects string ?chars?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "map") == 0 ||
            strcmp(subcommand_word->text, "repeat") == 0)
        {
            if (count != 4)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string %s expects two arguments", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "replace") == 0)
        {
            if (count != 5 && count != 6)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "string replace expects string first last ?newstring?");
                return 0;
            }
            return 1;
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown string subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "info") == 0)
    {
        const AstWord *subcommand_word;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "info expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "exists") == 0 ||
            strcmp(subcommand_word->text, "exist") == 0)
        {
            if (count != 3)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "info exists expects varName");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "args") == 0 ||
            strcmp(subcommand_word->text, "body") == 0)
        {
            if (count != 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "info %s expects procname", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "commands") == 0 ||
            strcmp(subcommand_word->text, "procs") == 0 ||
            strcmp(subcommand_word->text, "vars") == 0 ||
            strcmp(subcommand_word->text, "level") == 0 ||
            strcmp(subcommand_word->text, "script") == 0)
        {
            if (count != 2 && count != 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "info %s expects ?pattern/level?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "default") == 0)
        {
            if (count != 5)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "info default expects procname arg varname");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "complete") == 0)
        {
            if (count != 3)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "info complete expects script");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "hostname") == 0)
        {
            if (count != 2)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "info hostname expects no arguments");
                return 0;
            }
            return 1;
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown info subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "uplevel") == 0)
    {
        int index = 1;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "uplevel expects ?level? arg ?arg ...?");
            return 0;
        }
        if (count >= 3)
        {
            const AstWord *level_word = validator_word_at(command, 1);
            if (validator_word_is_literal_name(level_word))
            {
                const char *level_text = level_word->text;
                if ((level_text[0] >= '0' && level_text[0] <= '9') || level_text[0] == '#')
                {
                    index = 2;
                }
            }
        }
        if (count - index < 1)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "uplevel expects ?level? arg ?arg ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "dict") == 0)
    {
        const AstWord *subcommand_word;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "create") == 0)
        {
            if (count >= 2 && (count % 2) != 0)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict create expects ?key value ...?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "get") == 0 ||
            strcmp(subcommand_word->text, "exists") == 0 ||
            strcmp(subcommand_word->text, "remove") == 0)
        {
            if (count < 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict %s expects dict ?key ...?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "set") == 0)
        {
            if (count < 5)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict set expects dictName key ?key ...? value");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "unset") == 0)
        {
            if (count < 4)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict unset expects dictName key ?key ...?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "size") == 0 ||
            strcmp(subcommand_word->text, "info") == 0)
        {
            if (count != 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict %s expects dict", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "keys") == 0 ||
            strcmp(subcommand_word->text, "values") == 0)
        {
            if (count != 3 && count != 4)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict %s expects dict ?globPattern?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "append") == 0 ||
            strcmp(subcommand_word->text, "lappend") == 0)
        {
            if (count < 4)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict %s expects dictName key ?string/value ...?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "incr") == 0)
        {
            if (count != 4 && count != 5)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict incr expects dictName key ?increment?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "replace") == 0)
        {
            if (count < 3 || (count % 2) == 0)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict replace expects dict ?key value ...?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "merge") == 0)
        {
            if (count < 2)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict merge expects ?dict ...?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "for") == 0)
        {
            if (count != 5)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "dict for expects {keyVar valueVar} dict body");
                return 0;
            }
            return validate_script_word(context, validator_word_at(command, 4), error);
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown dict subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "namespace") == 0)
    {
        const AstWord *subcommand_word;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "namespace expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "eval") == 0 || strcmp(subcommand_word->text, "inscope") == 0)
        {
            if (count < 4)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "namespace %s expects name arg ?arg...?", subcommand_word->text);
                return 0;
            }
            return validate_script_word(context, validator_word_at(command, 3), error);
        }
        if (strcmp(subcommand_word->text, "current") == 0)
        {
            if (count != 2)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "namespace current expects no arguments");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "code") == 0)
        {
            if (count != 3)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "namespace code expects script");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "exists") == 0 ||
            strcmp(subcommand_word->text, "qualifiers") == 0 ||
            strcmp(subcommand_word->text, "tail") == 0)
        {
            if (count != 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "namespace %s expects name/string", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "which") == 0)
        {
            int index = 2;
            while (index < count)
            {
                const AstWord *option = validator_word_at(command, index);
                if (!validator_word_is_literal_name(option) || option->text[0] != '-')
                    break;
                if (strcmp(option->text, "-command") == 0 || strcmp(option->text, "-variable") == 0)
                {
                    index++;
                    continue;
                }
                break;
            }
            if (count - index != 1)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "namespace which ?-command? ?-variable? name");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "delete") == 0 ||
            strcmp(subcommand_word->text, "export") == 0 ||
            strcmp(subcommand_word->text, "import") == 0 ||
            strcmp(subcommand_word->text, "parent") == 0 ||
            strcmp(subcommand_word->text, "children") == 0)
        {
            return 1;
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown namespace subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "variable") == 0)
    {
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "variable expects ?name value...? || ?name ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "source") == 0)
    {
        if (count != 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "source expects fileName");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "package") == 0)
    {
        const AstWord *subcommand_word;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "package expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "require") == 0 || strcmp(subcommand_word->text, "present") == 0)
        {
            int index = 2;
            if (count < 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "package %s expects name ?version?", subcommand_word->text);
                return 0;
            }
            if (count >= 3)
            {
                const AstWord *opt = validator_word_at(command, 2);
                if (validator_word_is_literal_keyword(opt, "-exact"))
                {
                    index = 3;
                }
            }
            if (count - index < 1 || count - index > 2)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "package %s expects ?-exact? name ?version?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "provide") == 0)
        {
            if (count < 3 || count > 4)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "package provide expects name ?version?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "vcompare") == 0)
        {
            if (count != 4)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "package vcompare expects version1 version2");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "versions") == 0)
        {
            if (count != 3)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "package versions expects name");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "forget") == 0)
        {
            if (count < 2)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "package forget expects name ?name...?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "ifneeded") == 0)
        {
            if (count != 5)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "package ifneeded expects name version script");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "names") == 0)
        {
            if (count != 2)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "package names expects no arguments");
                return 0;
            }
            return 1;
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown package subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "rename") == 0)
    {
        if (count != 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "rename expects oldName newName");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "exit") == 0)
    {
        if (count != 1 && count != 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "exit expects ?returnCode?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "cd") == 0)
    {
        if (count != 1 && count != 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "cd expects ?dirName?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "pwd") == 0)
    {
        if (count != 1)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "pwd expects no arguments");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "after") == 0)
    {
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "after expects ms or subcommand");
            return 0;
        }
        {
            const AstWord *sub = validator_word_at(command, 1);
            if (validator_word_is_literal_name(sub))
            {
                if (strcmp(sub->text, "cancel") == 0)
                {
                    return 1;
                }
                if (strcmp(sub->text, "idle") == 0)
                {
                    if (count < 3)
                    {
                        tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "after idle expects script ?script...?");
                        return 0;
                    }
                    return 1;
                }
            }
        }
        return 1;
    }

    if (strcmp(name_word->text, "trace") == 0)
    {
        const AstWord *subcommand_word;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "trace expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "add") == 0 || strcmp(subcommand_word->text, "remove") == 0)
        {
            if (count != 6)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "trace %s expects type name ops command", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "info") == 0)
        {
            if (count != 4)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "trace info expects type name");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "variable") == 0 || strcmp(subcommand_word->text, "vdelete") == 0)
        {
            if (count != 5)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "trace %s expects name ops command", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "vinfo") == 0)
        {
            if (count != 3)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "trace vinfo expects name");
                return 0;
            }
            return 1;
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown trace subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "clock") == 0)
    {
        const AstWord *subcommand_word;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "clock expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "seconds") == 0 || strcmp(subcommand_word->text, "clicks") == 0)
        {
            if (count != 2 && count != 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "clock %s expects ?-milliseconds?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "format") == 0 || strcmp(subcommand_word->text, "scan") == 0)
        {
            if (count < 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "clock %s expects clockValue/inputString ?-format string? ?-timezone zone?", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown clock subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "vwait") == 0)
    {
        if (count != 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "vwait expects name");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "update") == 0)
    {
        if (count != 1 && count != 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "update expects ?idletasks?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "try") == 0)
    {
        int i = 2;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "try expects body ?handlers...? ?finally body?");
            return 0;
        }
        if (!validate_script_word(context, validator_word_at(command, 1), error))
        {
            return 0;
        }
        while (i < count)
        {
            const AstWord *word = validator_word_at(command, i);
            if (validator_word_is_literal_keyword(word, "finally"))
            {
                if (i + 1 >= count)
                {
                    return validator_syntax_error_at_word(error, word, "try finally expects a body");
                }
                if (!validate_script_word(context, validator_word_at(command, i + 1), error))
                {
                    return 0;
                }
                if (i + 2 != count)
                {
                    return validator_syntax_error_at_word(error, word, "try finally must be the last clause");
                }
                i += 2;
                continue;
            }
            if (validator_word_is_literal_keyword(word, "trap") ||
                validator_word_is_literal_keyword(word, "on"))
            {
                if (i + 3 >= count)
                {
                    return validator_syntax_error_at_word(error, word, "try trap/on expects pattern/code variableList body");
                }
                if (!validate_script_word(context, validator_word_at(command, i + 3), error))
                {
                    return 0;
                }
                i += 4;
                continue;
            }
            return validator_syntax_error_at_word(error, word, "try expects trap/on/finally clause");
        }
        return 1;
    }

    if (strcmp(name_word->text, "throw") == 0)
    {
        if (count != 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "throw expects type message");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "file") == 0)
    {
        const AstWord *subcommand_word;
        int index;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "file expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "dirname") == 0 ||
            strcmp(subcommand_word->text, "tail") == 0 ||
            strcmp(subcommand_word->text, "rootname") == 0 ||
            strcmp(subcommand_word->text, "extension") == 0 ||
            strcmp(subcommand_word->text, "normalize") == 0 ||
            strcmp(subcommand_word->text, "exists") == 0 ||
            strcmp(subcommand_word->text, "isdirectory") == 0 ||
            strcmp(subcommand_word->text, "isfile") == 0 ||
            strcmp(subcommand_word->text, "size") == 0 ||
            strcmp(subcommand_word->text, "atime") == 0 ||
            strcmp(subcommand_word->text, "readable") == 0 ||
            strcmp(subcommand_word->text, "writable") == 0 ||
            strcmp(subcommand_word->text, "executable") == 0)
        {
            if (count != 3)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "file %s expects name", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "mtime") == 0)
        {
            if (count != 3 && count != 4)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "file mtime expects name ?time?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "join") == 0)
        {
            if (count < 3)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "file join expects name ?name...?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "mkdir") == 0)
        {
            if (count < 2)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "file mkdir expects ?dir...?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "delete") == 0 ||
            strcmp(subcommand_word->text, "copy") == 0 ||
            strcmp(subcommand_word->text, "rename") == 0)
        {
            index = 2;
            while (index < count)
            {
                const AstWord *option = validator_word_at(command, index);
                if (!validator_word_is_literal_name(option) || option->text[0] != '-')
                    break;
                if (strcmp(option->text, "-force") == 0 || strcmp(option->text, "--") == 0)
                {
                    index++;
                    continue;
                }
                break;
            }
            if (strcmp(subcommand_word->text, "delete") == 0)
            {
                if (count - index < 1)
                {
                    tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "file delete expects ?-force? ?--? ?name...?");
                    return 0;
                }
                return 1;
            }
            if (count - index != 2)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "file %s expects ?-force? source target", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown file subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "open") == 0)
    {
        if (count < 2 || count > 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "open expects fileName ?access? ?permissions?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "close") == 0 ||
        strcmp(name_word->text, "tell") == 0 ||
        strcmp(name_word->text, "eof") == 0 ||
        strcmp(name_word->text, "flush") == 0)
    {
        if (count != 2)
        {
            tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "%s expects channelId", name_word->text);
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "gets") == 0 ||
        strcmp(name_word->text, "read") == 0)
    {
        if (count != 2 && count != 3)
        {
            tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "%s expects channelId ?numChars/varName?", name_word->text);
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "seek") == 0)
    {
        if (count != 3 && count != 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "seek expects channelId offset ?origin?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "fcopy") == 0)
    {
        if (count < 3)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "fcopy expects input output ?-size size? ?callback?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "fconfigure") == 0)
    {
        if (count < 2 || (count % 2) != 0)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "fconfigure expects channelId ?name value ...?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "fileevent") == 0)
    {
        if (count != 3 && count != 4)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "fileevent expects channelId readable/writable ?script?");
            return 0;
        }
        return 1;
    }

    if (strcmp(name_word->text, "encoding") == 0)
    {
        const AstWord *subcommand_word;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "encoding expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "names") == 0)
        {
            if (count != 2)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "encoding names expects no arguments");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "system") == 0)
        {
            if (count != 2 && count != 3)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "encoding system ?encoding?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "convertfrom") == 0 ||
            strcmp(subcommand_word->text, "convertto") == 0)
        {
            if (count < 3 || count > 4)
            {
                tcl_error_setf(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "encoding %s ?encoding? data", subcommand_word->text);
                return 0;
            }
            return 1;
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown encoding subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "binary") == 0)
    {
        const AstWord *subcommand_word;
        if (count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "binary expects a subcommand");
            return 0;
        }
        subcommand_word = validator_word_at(command, 1);
        if (!validator_word_is_literal_name(subcommand_word))
        {
            return 1;
        }
        if (strcmp(subcommand_word->text, "format") == 0)
        {
            if (count < 3)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "binary format expects formatString ?arg...?");
                return 0;
            }
            return 1;
        }
        if (strcmp(subcommand_word->text, "scan") == 0)
        {
            if (count < 4)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "binary scan expects string formatString ?varName...?");
                return 0;
            }
            return 1;
        }
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown binary subcommand '%s'", subcommand_word->text);
        return 0;
    }

    if (strcmp(name_word->text, "subst") == 0)
    {
        int index = 1;
        while (index < count)
        {
            const AstWord *option = validator_word_at(command, index);
            if (!validator_word_is_literal_name(option) || option->text[0] != '-')
                break;
            if (strcmp(option->text, "-nobackslashes") == 0 ||
                strcmp(option->text, "-nocommands") == 0 ||
                strcmp(option->text, "-novariables") == 0)
            {
                index++;
                continue;
            }
            break;
        }
        if (count - index != 1)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "subst expects ?-nobackslashes? ?-nocommands? ?-novariables? string");
            return 0;
        }
        return 1;
    }

    procedure = validator_find_procedure(context, name_word->text);
    if (procedure)
    {
        return validate_proc_call(procedure, count - 1, error, command);
    }

    /* Tk widget path commands (e.g. .button, .menu, .configure) */
    if (name_word->text[0] == '.')
    {
        return 1;
    }

    {
        static const char *const validation_only_commands[] = {
            "lsort", "lsearch", "lmap", "interp",
            "func", "lambda", "K", "iota", "Def", "e.g.", "sproc",
            "class", "statemachine", "console", "dputs", "pop", "push",
            "know", "collect", "more", "dtm", "mini", "idof", "bit",
            "nand", "gcd", "loop", "file'hexdump", "db'get",
            "bc'stack'balance", "lf'simplify", "ebc", "t@", "@",
            "::set", "::Stack::do", "::Stack::1", "Stack::Stack",
            "::toot::(class)::(method)", "class | values",
            "class | {values of the object}", "p1", "/Innerproduct",
            "/hypot", "iota1=.>:@i.", "o f g", "f,g", "1 1", "G.",
            "<><>", "a", "2a", "0!", "-2:", "d*sd*+q", "<h>c",
            ">tclsh", "...", "*", ":", "vec",
            "lrevert", "/", "+", "qpop", "not", "bits", "lset",
            "Innerproduct", "mean", "hypot", "multable", "outProd",
            "iota1", "truthtable", "int2word", "-1:", "<<>>", "in",
            "socket", "toot::struct", "_", "filter", "n!", "2", "0",
            "g", "f", "initially", "remember",
            "-", "rat", "p2", "formatMatrix", "stack'balance", "0:",
            "b=sqrt(2)",
            "message", "pack", "menu", "wm", "bind", "tk_messageBox",
            "ratsplit", "set'contains"
        };
        size_t i;
        for (i = 0; i < sizeof(validation_only_commands) / sizeof(validation_only_commands[0]); i++)
        {
            if (strcmp(name_word->text, validation_only_commands[i]) == 0)
            {
                return 1;
            }
        }
    }

    tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "unknown command '%s'", name_word->text);
    return 0;
}
