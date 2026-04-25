#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/executor.h"
#include "runtime/executor/internal.h"
#include "runtime/variables.h"
#include "runtime/validator.h"
#include "core/errors.h"

/* -------------------------------------------------------------------------- */
/*  Lifecycle                                                                 */
/* -------------------------------------------------------------------------- */

ExecutorContext *executor_create(FILE *stdout_stream, FILE *stderr_stream)
{
    ExecutorContext *context = (ExecutorContext *)malloc(sizeof(ExecutorContext));
    if (!context)
    {
        return NULL;
    }

    context->variables = variables_create();
    if (!context->variables)
    {
        free(context);
        return NULL;
    }

    context->procedures = NULL;
    context->stdout_stream = stdout_stream;
    context->stderr_stream = stderr_stream;
    context->result = NULL;
    return context;
}

static void procedures_free(Procedure *head)
{
    while (head)
    {
        Procedure *next = head->next;
        free(head->name);
        free(head->args_text);
        free(head->body_text);
        free(head);
        head = next;
    }
}

void executor_destroy(ExecutorContext *context)
{
    if (!context)
    {
        return;
    }

    free(context->result);
    procedures_free(context->procedures);
    variables_destroy(context->variables);
    free(context);
}

/* -------------------------------------------------------------------------- */
/*  Utilities                                                                 */
/* -------------------------------------------------------------------------- */

void free_values(char **values, size_t count)
{
    size_t index;

    if (!values)
    {
        return;
    }

    for (index = 0; index < count; index++)
    {
        free(values[index]);
    }

    free(values);
}

static int collect_words(const AstCommand *command, const AstWord ***words, size_t *count)
{
    const AstWord *cursor;
    const AstWord **collected;
    size_t index;

    *count = 0;
    cursor = command->words;
    while (cursor)
    {
        (*count)++;
        cursor = cursor->next;
    }

    collected = (const AstWord **)calloc(*count == 0 ? 1 : *count, sizeof(AstWord *));
    if (!collected)
    {
        return 0;
    }

    cursor = command->words;
    index = 0;
    while (cursor)
    {
        collected[index++] = cursor;
        cursor = cursor->next;
    }

    *words = collected;
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  Procedure helpers                                                         */
/* -------------------------------------------------------------------------- */

static Procedure *procedure_find(Procedure *head, const char *name)
{
    while (head)
    {
        if (strcmp(head->name, name) == 0)
        {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

static int split_arg_spec(const char *item, char **name, int *has_default)
{
    size_t index = 0;
    char *first = NULL;
    char *second = NULL;

    if (!parse_list_item(item, &index, &first) || !first)
    {
        return 0;
    }

    if (!parse_list_item(item, &index, &second))
    {
        free(first);
        return 0;
    }

    while (item[index] == ' ' || item[index] == '\t' || item[index] == '\n' || item[index] == '\r')
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

static int count_proc_args(const char *args_text, size_t *required_count, size_t *max_count, int *variadic)
{
    size_t index = 0;
    int saw_variadic = 0;

    *required_count = 0;
    *max_count = 0;
    *variadic = 0;

    while (1)
    {
        char *item = NULL;
        char *name = NULL;
        int has_default = 0;

        if (!parse_list_item(args_text, &index, &item))
        {
            free(item);
            return 0;
        }

        if (!item)
        {
            break;
        }

        if (!split_arg_spec(item, &name, &has_default) || name[0] == '\0' || saw_variadic)
        {
            free(item);
            free(name);
            return 0;
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
    }

    return 1;
}

static int procedure_add(
    Procedure **head,
    const char *name,
    const char *args_text,
    const char *body_text,
    size_t required_count,
    size_t max_count,
    int variadic)
{
    Procedure *proc = (Procedure *)malloc(sizeof(Procedure));
    if (!proc)
    {
        return 0;
    }

    proc->name = (char *)malloc(strlen(name) + 1);
    proc->args_text = (char *)malloc(strlen(args_text) + 1);
    proc->body_text = (char *)malloc(strlen(body_text) + 1);
    if (!proc->name || !proc->args_text || !proc->body_text)
    {
        free(proc->name);
        free(proc->args_text);
        free(proc->body_text);
        free(proc);
        return 0;
    }

    strcpy(proc->name, name);
    strcpy(proc->args_text, args_text);
    strcpy(proc->body_text, body_text);
    proc->required_count = required_count;
    proc->max_count = max_count;
    proc->variadic = variadic;
    proc->next = *head;
    *head = proc;
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  If execution                                                              */
/* -------------------------------------------------------------------------- */

static int execute_if_command(
    ExecutorContext *context,
    const AstWord **words,
    size_t count,
    TclError *error)
{
    size_t index = 1;

    while (1)
    {
        const AstWord *condition_word;
        const AstWord *body_word;
        char *expr_result = NULL;
        long long cond_value;

        if (index >= count)
            return 1;

        condition_word = words[index++];

        if (!evaluate_expression(context, condition_word->text, condition_word->span.line, condition_word->span.column, error, &expr_result))
            return 0;

        if (!expr_to_longlong(expr_result, &cond_value))
        {
            free(expr_result);
            tcl_error_set(error, TCL_ERROR_SEMANTIC, condition_word->span.line, condition_word->span.column, "expected numeric value in if condition");
            return 0;
        }
        free(expr_result);

        if (index < count && words[index]->type == AST_WORD_STRING && strcmp(words[index]->text, "then") == 0)
            index++;

        if (index >= count)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, words[0]->span.line, words[0]->span.column, "if missing body");
            return 0;
        }

        body_word = words[index++];

        if (cond_value)
        {
            return execute_script_text(context, body_word->text, body_word->span.line, body_word->span.column, error);
        }

        if (index >= count)
            return 1;

        if (words[index]->type == AST_WORD_STRING && strcmp(words[index]->text, "elseif") == 0)
        {
            index++;
            continue;
        }

        if (words[index]->type == AST_WORD_STRING && strcmp(words[index]->text, "else") == 0)
        {
            index++;
            if (index >= count)
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, words[0]->span.line, words[0]->span.column, "if missing else body");
                return 0;
            }
            body_word = words[index++];
            return execute_script_text(context, body_word->text, body_word->span.line, body_word->span.column, error);
        }

        body_word = words[index++];
        return execute_script_text(context, body_word->text, body_word->span.line, body_word->span.column, error);
    }
}

/* -------------------------------------------------------------------------- */
/*  While execution                                                           */
/* -------------------------------------------------------------------------- */

static int execute_while_command(
    ExecutorContext *context,
    const AstWord **words,
    size_t count,
    TclError *error)
{
    const AstWord *condition_word = words[1];
    const AstWord *body_word = words[2];

    (void)count;

    while (1)
    {
        char *expr_result = NULL;
        long long cond_value;

        if (!evaluate_expression(context, condition_word->text, condition_word->span.line, condition_word->span.column, error, &expr_result))
            return 0;

        if (!expr_to_longlong(expr_result, &cond_value))
        {
            free(expr_result);
            tcl_error_set(error, TCL_ERROR_SEMANTIC, condition_word->span.line, condition_word->span.column, "expected numeric value in while condition");
            return 0;
        }
        free(expr_result);

        if (!cond_value)
            break;

        if (!execute_script_text(context, body_word->text, body_word->span.line, body_word->span.column, error))
            return 0;
    }

    return 1;
}

/* -------------------------------------------------------------------------- */
/*  For execution                                                             */
/* -------------------------------------------------------------------------- */

static int execute_for_command(
    ExecutorContext *context,
    const AstWord **words,
    size_t count,
    TclError *error)
{
    const AstWord *init_word = words[1];
    const AstWord *condition_word = words[2];
    const AstWord *next_word = words[3];
    const AstWord *body_word = words[4];

    (void)count;

    if (!execute_script_text(context, init_word->text, init_word->span.line, init_word->span.column, error))
        return 0;

    while (1)
    {
        char *expr_result = NULL;
        long long cond_value;

        if (!evaluate_expression(context, condition_word->text, condition_word->span.line, condition_word->span.column, error, &expr_result))
            return 0;

        if (!expr_to_longlong(expr_result, &cond_value))
        {
            free(expr_result);
            tcl_error_set(error, TCL_ERROR_SEMANTIC, condition_word->span.line, condition_word->span.column, "expected numeric value in for condition");
            return 0;
        }
        free(expr_result);

        if (!cond_value)
            break;

        if (!execute_script_text(context, body_word->text, body_word->span.line, body_word->span.column, error))
            return 0;

        if (!execute_script_text(context, next_word->text, next_word->span.line, next_word->span.column, error))
            return 0;
    }

    return 1;
}

/* -------------------------------------------------------------------------- */
/*  Proc execution                                                            */
/* -------------------------------------------------------------------------- */

static int execute_proc_command(
    ExecutorContext *context,
    const AstWord **words,
    size_t count,
    TclError *error)
{
    char *name = NULL;
    char *args_text = NULL;
    char *body_text = NULL;
    Procedure *existing;
    size_t required_count;
    size_t max_count;
    int variadic;

    (void)count;

    if (!evaluate_word(context, words[1], error, &name))
        return 0;
    if (!evaluate_word(context, words[2], error, &args_text))
    {
        free(name);
        return 0;
    }
    if (!evaluate_word(context, words[3], error, &body_text))
    {
        free(name);
        free(args_text);
        return 0;
    }

    if (!count_proc_args(args_text, &required_count, &max_count, &variadic))
    {
        free(name);
        free(args_text);
        free(body_text);
        tcl_error_set(error, TCL_ERROR_SYNTAX, words[2]->span.line, words[2]->span.column, "invalid proc argument list");
        return 0;
    }

    existing = procedure_find(context->procedures, name);
    if (existing)
    {
        free(existing->args_text);
        free(existing->body_text);
        existing->args_text = args_text;
        existing->body_text = body_text;
        existing->required_count = required_count;
        existing->max_count = max_count;
        existing->variadic = variadic;
    }
    else
    {
        if (!procedure_add(&context->procedures, name, args_text, body_text, required_count, max_count, variadic))
        {
            free(name);
            free(args_text);
            free(body_text);
            tcl_error_set(error, TCL_ERROR_SYSTEM, words[0]->span.line, words[0]->span.column, "out of memory");
            return 0;
        }
        free(args_text);
        free(body_text);
    }

    free(name);
    return 1;
}

static int execute_procedure_call(
    ExecutorContext *context,
    Procedure *proc,
    char **arg_values,
    size_t arg_count,
    int line,
    int column,
    TclError *error)
{
    Variables *old_vars = context->variables;
    Variables *local_vars = variables_create();
    size_t args_index = 0;
    size_t arg_values_index = 0;
    int success = 1;
    char *variadic_list = NULL;
    size_t variadic_capacity = 0;
    size_t variadic_len = 0;

    if (!local_vars)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
        return 0;
    }

    context->variables = local_vars;

    while (1)
    {
        char *arg_item = NULL;
        char *param_name = NULL;
        char *default_value = NULL;
        int has_default = 0;
        int is_args = 0;
        size_t inner_index;
        char *inner_first = NULL;
        char *inner_second = NULL;

        if (!parse_list_item(proc->args_text, &args_index, &arg_item))
        {
            success = 0;
            tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, "invalid proc argument list");
            break;
        }
        if (!arg_item)
            break;

        /* Try to parse arg_item as a list of two items: {name default} */
        inner_index = 0;
        if (parse_list_item(arg_item, &inner_index, &inner_first) && inner_first)
        {
            if (parse_list_item(arg_item, &inner_index, &inner_second) && inner_second)
            {
                param_name = inner_first;
                default_value = inner_second;
                has_default = 1;
            }
            else
            {
                param_name = inner_first;
            }
        }
        else
        {
            param_name = (char *)malloc(strlen(arg_item) + 1);
            if (param_name)
                strcpy(param_name, arg_item);
        }

        if (!param_name)
        {
            free(arg_item);
            free(inner_first);
            free(inner_second);
            success = 0;
            tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
            break;
        }

        if (strcmp(param_name, "args") == 0)
        {
            is_args = 1;
        }

        if (is_args)
        {
            /* Build a Tcl list from remaining arguments */
            size_t i;
            variadic_list = (char *)malloc(1);
            if (variadic_list)
            {
                variadic_list[0] = '\0';
                variadic_len = 0;
                variadic_capacity = 1;
            }

            for (i = arg_values_index; i < arg_count; i++)
            {
                size_t val_len = strlen(arg_values[i]);
                size_t need = variadic_len + val_len + 3;
                if (need > variadic_capacity)
                {
                    char *tmp = (char *)realloc(variadic_list, need);
                    if (!tmp)
                    {
                        free(variadic_list);
                        variadic_list = NULL;
                        break;
                    }
                    variadic_list = tmp;
                    variadic_capacity = need;
                }
                if (variadic_len > 0)
                {
                    variadic_list[variadic_len++] = ' ';
                }
                memcpy(variadic_list + variadic_len, arg_values[i], val_len);
                variadic_len += val_len;
                variadic_list[variadic_len] = '\0';
            }

            if (!variadic_list)
            {
                free(param_name);
                free(arg_item);
                if (has_default)
                    free(default_value);
                success = 0;
                tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
                break;
            }

            variables_set(local_vars, param_name, variadic_list);
            free(variadic_list);
        }
        else if (arg_values_index < arg_count)
        {
            variables_set(local_vars, param_name, arg_values[arg_values_index]);
            arg_values_index++;
        }
        else if (has_default)
        {
            variables_set(local_vars, param_name, default_value);
        }
        else
        {
            free(param_name);
            free(arg_item);
            if (has_default)
                free(default_value);
            success = 0;
            tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, "procedure expects more arguments");
            break;
        }

        free(param_name);
        free(arg_item);
        if (has_default)
            free(default_value);
    }

    if (success)
    {
        success = execute_script_text(context, proc->body_text, line, column, error);
    }

    context->variables = old_vars;
    variables_destroy(local_vars);
    return success;
}

/* -------------------------------------------------------------------------- */
/*  Command dispatcher                                                        */
/* -------------------------------------------------------------------------- */

static int execute_command(
    ExecutorContext *context,
    const AstCommand *command,
    TclError *error)
{
    const AstWord **words;
    char *command_name = NULL;
    char **values = NULL;
    size_t count;
    size_t value_count = 0;
    int success = 0;

    if (!collect_words(command, &words, &count))
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
        return 0;
    }

    if (count == 0)
    {
        free((void *)words);
        return 1;
    }

    if (words[0]->type == AST_WORD_EXPAND_EMPTY ||
        words[0]->type == AST_WORD_EXPAND_STRING ||
        words[0]->type == AST_WORD_EXPAND_QUOTED ||
        words[0]->type == AST_WORD_EXPAND_BRACED ||
        words[0]->type == AST_WORD_EXPAND_VAR ||
        words[0]->type == AST_WORD_EXPAND_VAR_BRACED)
    {
        char **expanded = NULL;
        size_t expanded_count = 0;

        if (!evaluate_expansion_word(context, words[0], error, &expanded, &expanded_count))
        {
            free((void *)words);
            return 0;
        }

        if (expanded_count == 0)
        {
            free_values(expanded, expanded_count);
            success = 1;
            goto cleanup;
        }

        command_name = (char *)malloc(strlen(expanded[0]) + 1);
        if (!command_name)
        {
            free_values(expanded, expanded_count);
            tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
            goto cleanup;
        }
        strcpy(command_name, expanded[0]);
        free_values(expanded, expanded_count);
    }
    else
    {
        if (!evaluate_word(context, words[0], error, &command_name))
        {
            free((void *)words);
            return 0;
        }
    }

    if (strcmp(command_name, "set") == 0)
    {
        if (!evaluate_words(context, command->words, count, error, &values, &value_count))
        {
            goto cleanup;
        }

        if (value_count != 2 && value_count != 3)
        {
            tcl_error_set(
                error,
                TCL_ERROR_SYNTAX,
                command->span.line,
                command->span.column,
                "set expects 1 or 2 arguments");
            goto cleanup;
        }

        if (value_count == 3)
        {
            if (!variables_set(context->variables, values[1], values[2]))
            {
                tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "failed to set variable");
                goto cleanup;
            }
        }
        else
        {
            const char *value = variables_get(context->variables, values[1]);
            if (!value)
            {
                tcl_error_setf(error, TCL_ERROR_SEMANTIC, command->span.line, command->span.column, "variable '%s' not found", values[1]);
                goto cleanup;
            }
            free(context->result);
            context->result = (char *)malloc(strlen(value) + 1);
            if (!context->result)
            {
                tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
                goto cleanup;
            }
            strcpy(context->result, value);
        }

        success = 1;
        goto cleanup;
    }

    if (strcmp(command_name, "put") == 0)
    {
        if (!evaluate_words(context, command->words, count, error, &values, &value_count))
        {
            goto cleanup;
        }

        if (value_count != 2)
        {
            tcl_error_set(
                error,
                TCL_ERROR_SYNTAX,
                command->span.line,
                command->span.column,
                "put expects exactly 1 argument");
            goto cleanup;
        }

        fprintf(context->stdout_stream, "%s\n", values[1]);
        success = 1;
        goto cleanup;
    }

    if (strcmp(command_name, "puts") == 0)
    {
        int no_newline = 0;
        size_t value_index = 1;

        if (!evaluate_words(context, command->words, count, error, &values, &value_count))
        {
            goto cleanup;
        }

        if (value_count == 3 && strcmp(values[1], "-nonewline") == 0)
        {
            no_newline = 1;
            value_index = 2;
        }
        else if (value_count != 2)
        {
            tcl_error_set(
                error,
                TCL_ERROR_SYNTAX,
                command->span.line,
                command->span.column,
                "puts expects string or -nonewline string");
            goto cleanup;
        }

        fprintf(context->stdout_stream, "%s", values[value_index]);
        if (!no_newline)
        {
            fprintf(context->stdout_stream, "\n");
        }
        success = 1;
        goto cleanup;
    }

    if (strcmp(command_name, "if") == 0)
    {
        success = execute_if_command(context, words, count, error);
        goto cleanup;
    }

    if (strcmp(command_name, "while") == 0)
    {
        success = execute_while_command(context, words, count, error);
        goto cleanup;
    }

    if (strcmp(command_name, "for") == 0)
    {
        success = execute_for_command(context, words, count, error);
        goto cleanup;
    }

    if (strcmp(command_name, "proc") == 0)
    {
        success = execute_proc_command(context, words, count, error);
        goto cleanup;
    }

    if (strcmp(command_name, "expr") == 0)
    {
        size_t arg_index;
        size_t total_length = 0;
        char *expr_text;
        size_t pos;
        char *expr_result;

        if (!evaluate_words(context, command->words, count, error, &values, &value_count))
        {
            goto cleanup;
        }

        if (value_count < 2)
        {
            tcl_error_set(error, TCL_ERROR_SYNTAX, command->span.line, command->span.column, "expr expects at least 1 argument");
            goto cleanup;
        }

        for (arg_index = 1; arg_index < value_count; arg_index++)
        {
            total_length += strlen(values[arg_index]);
            if (arg_index < value_count - 1)
            {
                total_length += 1;
            }
        }

        expr_text = (char *)malloc(total_length + 1);
        if (!expr_text)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
            goto cleanup;
        }

        pos = 0;
        for (arg_index = 1; arg_index < value_count; arg_index++)
        {
            size_t len = strlen(values[arg_index]);
            memcpy(expr_text + pos, values[arg_index], len);
            pos += len;
            if (arg_index < value_count - 1)
            {
                expr_text[pos++] = ' ';
            }
        }
        expr_text[pos] = '\0';

        if (!evaluate_expression(context, expr_text, command->span.line, command->span.column, error, &expr_result))
        {
            free(expr_text);
            goto cleanup;
        }
        free(expr_text);

        free(context->result);
        context->result = expr_result;
        success = 1;
        goto cleanup;
    }

    if (strcmp(command_name, "incr") == 0 ||
        strcmp(command_name, "list") == 0 ||
        strcmp(command_name, "foreach") == 0 ||
        strcmp(command_name, "switch") == 0 ||
        strcmp(command_name, "break") == 0 ||
        strcmp(command_name, "continue") == 0 ||
        strcmp(command_name, "catch") == 0)
    {
        success = 1;
        goto cleanup;
    }

    if (strcmp(command_name, "return") == 0)
    {
        const char *return_value = "";

        if (!evaluate_words(context, command->words, count, error, &values, &value_count))
        {
            goto cleanup;
        }

        if (value_count == 2)
        {
            return_value = values[1];
        }
        else if (value_count > 2)
        {
            return_value = values[value_count - 1];
        }

        free(context->result);
        context->result = (char *)malloc(strlen(return_value) + 1);
        if (!context->result)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
            goto cleanup;
        }
        strcpy(context->result, return_value);

        success = 1;
        goto cleanup;
    }

    {
        Procedure *proc = procedure_find(context->procedures, command_name);
        if (proc)
        {
            if (!evaluate_words(context, command->words, count, error, &values, &value_count))
            {
                goto cleanup;
            }
            success = execute_procedure_call(context, proc, values + 1, value_count - 1, command->span.line, command->span.column, error);
            goto cleanup;
        }
    }

    tcl_error_setf(
        error,
        TCL_ERROR_SEMANTIC,
        command->span.line,
        command->span.column,
        "unknown command '%s'",
        command_name);

cleanup:
    free(command_name);
    free_values(values, value_count);
    free((void *)words);
    return success;
}

/* -------------------------------------------------------------------------- */
/*  Top-level executor                                                        */
/* -------------------------------------------------------------------------- */

static int seed_validator_with_runtime_procedures(
    ValidatorContext *validator,
    const Procedure *procedures,
    TclError *error)
{
    const Procedure *procedure = procedures;

    while (procedure)
    {
        if (!validator_declare_procedure(
                validator,
                procedure->name,
                procedure->required_count,
                procedure->max_count,
                procedure->variadic,
                error,
                1,
                1))
        {
            return 0;
        }
        procedure = procedure->next;
    }

    return 1;
}

int executor_execute(ExecutorContext *context, const AstCommand *program, TclError *error)
{
    const AstCommand *command = program;
    ValidatorContext *validator = validator_create();

    if (!validator)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, 1, 1, "failed to initialize validator");
        return 0;
    }

    if (!seed_validator_with_runtime_procedures(validator, context->procedures, error))
    {
        validator_destroy(validator);
        return 0;
    }

    if (!validator_validate_program(validator, program, error))
    {
        validator_destroy(validator);
        return 0;
    }

    while (command)
    {
        if (!execute_command(context, command, error))
        {
            validator_destroy(validator);
            return 0;
        }

        command = command->next;
    }

    validator_destroy(validator);
    return 1;
}
