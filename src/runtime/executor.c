#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/errors.h"
#include "runtime/executor.h"
#include "runtime/executor/internal.h"
#include "runtime/validator.h"
#include "runtime/variables.h"


ExecutorContext *executor_create(FILE *stdin_stream, FILE *stdout_stream, FILE *stderr_stream)
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
    context->stdin_stream = stdin_stream;
    context->stdout_stream = stdout_stream;
    context->stderr_stream = stderr_stream;
    context->proc_depth = 0;
    context->loop_depth = 0;
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


static char *copy_string(const char *text)
{
    char *copy;

    if (!text)
    {
        text = "";
    }

    copy = (char *)malloc(strlen(text) + 1);
    if (!copy)
    {
        return NULL;
    }

    strcpy(copy, text);
    return copy;
}

static ExecCode set_system_error(TclError *error, int line, int column, const char *message)
{
    tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, message);
    return EXEC_CODE_ERROR;
}

static ExecCode set_syntax_error(TclError *error, int line, int column, const char *message)
{
    tcl_error_set(error, TCL_ERROR_SYNTAX, line, column, message);
    return EXEC_CODE_ERROR;
}

static ExecCode set_semantic_error(TclError *error, int line, int column, const char *message)
{
    tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, message);
    return EXEC_CODE_ERROR;
}

static ExecCode set_semantic_errorf(TclError *error, int line, int column, const char *format, const char *arg)
{
    tcl_error_setf(error, TCL_ERROR_SEMANTIC, line, column, format, arg);
    return EXEC_CODE_ERROR;
}

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

static int is_supported_var_char(char character)
{
    return isalnum((unsigned char)character) || character == '_';
}

static int is_supported_var_name(const char *name)
{
    size_t index;

    if (!name || name[0] == '\0')
    {
        return 0;
    }

    for (index = 0; name[index]; index++)
    {
        if (!is_supported_var_char(name[index]))
        {
            return 0;
        }
    }

    return 1;
}

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

static int count_proc_args(const char *args_text, size_t *arg_count)
{
    size_t index = 0;
    size_t count = 0;

    while (1)
    {
        char *item = NULL;

        if (!parse_list_item(args_text, &index, &item))
        {
            free(item);
            return 0;
        }

        if (!item)
        {
            break;
        }

        if (!is_supported_var_name(item) || strcmp(item, "args") == 0)
        {
            free(item);
            return 0;
        }

        free(item);
        count++;
    }

    *arg_count = count;
    return 1;
}

static int procedure_add_or_update(
    Procedure **head,
    const char *name,
    const char *args_text,
    const char *body_text,
    size_t arg_count)
{
    Procedure *existing = procedure_find(*head, name);

    if (existing)
    {
        char *new_args = copy_string(args_text);
        char *new_body = copy_string(body_text);
        if (!new_args || !new_body)
        {
            free(new_args);
            free(new_body);
            return 0;
        }

        free(existing->args_text);
        free(existing->body_text);
        existing->args_text = new_args;
        existing->body_text = new_body;
        existing->arg_count = arg_count;
        return 1;
    }

    {
        Procedure *proc = (Procedure *)malloc(sizeof(Procedure));
        if (!proc)
        {
            return 0;
        }

        proc->name = copy_string(name);
        proc->args_text = copy_string(args_text);
        proc->body_text = copy_string(body_text);
        if (!proc->name || !proc->args_text || !proc->body_text)
        {
            free(proc->name);
            free(proc->args_text);
            free(proc->body_text);
            free(proc);
            return 0;
        }

        proc->arg_count = arg_count;
        proc->next = *head;
        *head = proc;
        return 1;
    }
}

static ExecCode execute_command(
    ExecutorContext *context,
    const AstCommand *command,
    TclError *error,
    char **result);


ExecCode executor_execute_program(
    ExecutorContext *context,
    const AstCommand *program,
    TclError *error,
    char **result)
{
    const AstCommand *command = program;
    char *last_result = copy_string("");

    if (!last_result)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, 1, 1, "out of memory");
        return EXEC_CODE_ERROR;
    }

    while (command)
    {
        char *command_result = NULL;
        ExecCode code = execute_command(context, command, error, &command_result);

        if (code != EXEC_CODE_OK)
        {
            free(last_result);
            if (command_result)
            {
                *result = command_result;
            }
            else
            {
                *result = copy_string("");
            }
            return code;
        }

        free(last_result);
        last_result = command_result;
        if (!last_result)
        {
            return set_system_error(error, command->span.line, command->span.column, "out of memory");
        }

        command = command->next;
    }

    *result = last_result;
    return EXEC_CODE_OK;
}


static ExecCode evaluate_condition(
    ExecutorContext *context,
    const AstWord *condition_word,
    TclError *error,
    int *is_true)
{
    char *expr_result = NULL;
    long long cond_value;

    if (!evaluate_expression(
            context,
            condition_word->text,
            condition_word->span.line,
            condition_word->span.column,
            error,
            &expr_result))
    {
        return EXEC_CODE_ERROR;
    }

    if (!expr_to_longlong(expr_result, &cond_value))
    {
        free(expr_result);
        tcl_error_set(error, TCL_ERROR_SEMANTIC, condition_word->span.line, condition_word->span.column, "Expected boolean expression");
        return EXEC_CODE_ERROR;
    }

    free(expr_result);
    *is_true = cond_value != 0;
    return EXEC_CODE_OK;
}

static ExecCode execute_if_command(
    ExecutorContext *context,
    const AstWord **words,
    size_t count,
    TclError *error,
    char **result)
{
    size_t index = 1;

    while (1)
    {
        const AstWord *condition_word;
        const AstWord *body_word;
        int is_true = 0;
        ExecCode cond_code;

        if (index >= count)
        {
            *result = copy_string("");
            if (!*result)
            {
                return set_system_error(error, words[0]->span.line, words[0]->span.column, "out of memory");
            }
            return EXEC_CODE_OK;
        }

        condition_word = words[index++];
        cond_code = evaluate_condition(context, condition_word, error, &is_true);
        if (cond_code != EXEC_CODE_OK)
        {
            return cond_code;
        }

        if (index < count && words[index]->type == AST_WORD_STRING && strcmp(words[index]->text, "then") == 0)
        {
            index++;
        }

        if (index >= count)
        {
            return set_syntax_error(error, words[0]->span.line, words[0]->span.column, "if missing body");
        }

        body_word = words[index++];

        if (is_true)
        {
            char *body_result = NULL;
            ExecCode body_code = execute_script_text_with_code(
                context,
                body_word->text,
                body_word->span.line,
                body_word->span.column,
                error,
                &body_result);
            if (body_code != EXEC_CODE_OK)
            {
                *result = body_result;
                return body_code;
            }
            *result = body_result;
            if (!*result)
            {
                return set_system_error(error, body_word->span.line, body_word->span.column, "out of memory");
            }
            return EXEC_CODE_OK;
        }

        if (index >= count)
        {
            *result = copy_string("");
            if (!*result)
            {
                return set_system_error(error, words[0]->span.line, words[0]->span.column, "out of memory");
            }
            return EXEC_CODE_OK;
        }

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
                return set_syntax_error(error, words[0]->span.line, words[0]->span.column, "if missing else body");
            }

            body_word = words[index++];
            {
                char *body_result = NULL;
                ExecCode body_code = execute_script_text_with_code(
                    context,
                    body_word->text,
                    body_word->span.line,
                    body_word->span.column,
                    error,
                    &body_result);
                if (body_code != EXEC_CODE_OK)
                {
                    *result = body_result;
                    return body_code;
                }
                *result = body_result;
                if (!*result)
                {
                    return set_system_error(error, body_word->span.line, body_word->span.column, "out of memory");
                }
                return EXEC_CODE_OK;
            }
        }

        return set_syntax_error(error, words[index]->span.line, words[index]->span.column, "expected elseif or else");
    }
}

static int bind_proc_arguments(
    Variables *local_vars,
    const char *args_text,
    char **arg_values,
    size_t arg_count,
    TclError *error,
    int line,
    int column)
{
    size_t index = 0;
    size_t arg_index = 0;

    while (1)
    {
        char *name = NULL;

        if (!parse_list_item(args_text, &index, &name))
        {
            free(name);
            tcl_error_set(error, TCL_ERROR_SYNTAX, line, column, "invalid proc argument list");
            return 0;
        }

        if (!name)
        {
            break;
        }

        if (arg_index >= arg_count)
        {
            free(name);
            tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "argument binding mismatch");
            return 0;
        }

        if (!variables_set(local_vars, name, arg_values[arg_index]))
        {
            free(name);
            tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
            return 0;
        }

        free(name);
        arg_index++;
    }

    return 1;
}

static ExecCode execute_procedure_call(
    ExecutorContext *context,
    Procedure *proc,
    char **arg_values,
    size_t arg_count,
    int line,
    int column,
    TclError *error,
    char **result)
{
    Variables *old_vars = context->variables;
    Variables *local_vars = variables_create();
    char *body_result = NULL;
    ExecCode body_code;

    if (!local_vars)
    {
        return set_system_error(error, line, column, "out of memory");
    }

    if (arg_count != proc->arg_count)
    {
        char message[160];
        snprintf(
            message,
            sizeof(message),
            "Procedure \"%s\" expects %zu arguments, got %zu",
            proc->name,
            proc->arg_count,
            arg_count);
        variables_destroy(local_vars);
        return set_semantic_error(error, line, column, message);
    }

    if (!bind_proc_arguments(local_vars, proc->args_text, arg_values, arg_count, error, line, column))
    {
        variables_destroy(local_vars);
        return EXEC_CODE_ERROR;
    }

    context->variables = local_vars;
    context->proc_depth++;

    body_code = execute_script_text_with_code(context, proc->body_text, line, column, error, &body_result);

    context->proc_depth--;
    context->variables = old_vars;
    variables_destroy(local_vars);

    if (body_code == EXEC_CODE_RETURN)
    {
        *result = body_result ? body_result : copy_string("");
        if (!*result)
        {
            return set_system_error(error, line, column, "out of memory");
        }
        return EXEC_CODE_OK;
    }

    if (body_code == EXEC_CODE_OK)
    {
        *result = body_result ? body_result : copy_string("");
        if (!*result)
        {
            return set_system_error(error, line, column, "out of memory");
        }
        return EXEC_CODE_OK;
    }

    free(body_result);

    if (body_code == EXEC_CODE_BREAK)
    {
        return set_semantic_error(error, line, column, "break outside loop");
    }

    if (body_code == EXEC_CODE_CONTINUE)
    {
        return set_semantic_error(error, line, column, "continue outside loop");
    }

    return body_code;
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

static int any_expansion_word(const AstWord *word)
{
    while (word)
    {
        if (is_expand_word_type(word->type))
        {
            return 1;
        }
        word = word->next;
    }

    return 0;
}

static ExecCode builtin_set(
    ExecutorContext *context,
    const AstCommand *command,
    size_t count,
    TclError *error,
    char **result)
{
    char **values = NULL;
    size_t value_count = 0;

    if (!evaluate_words(context, command->words, count, error, &values, &value_count))
    {
        return EXEC_CODE_ERROR;
    }

    if (value_count != 2 && value_count != 3)
    {
        free_values(values, value_count);
        return set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command set");
    }

    if (!is_supported_var_name(values[1]))
    {
        free_values(values, value_count);
        return set_semantic_error(error, command->span.line, command->span.column, "array/namespace variables are not supported");
    }

    if (value_count == 3)
    {
        if (!variables_set(context->variables, values[1], values[2]))
        {
            free_values(values, value_count);
            return set_system_error(error, command->span.line, command->span.column, "out of memory");
        }

        *result = copy_string(values[2]);
    }
    else
    {
        const char *value = variables_get(context->variables, values[1]);
        if (!value)
        {
            char *name_copy = copy_string(values[1]);
            free_values(values, value_count);
            if (!name_copy)
            {
                return set_system_error(error, command->span.line, command->span.column, "out of memory");
            }
            {
                ExecCode code = set_semantic_errorf(
                    error,
                    command->span.line,
                    command->span.column,
                    "Undefined variable: %s",
                    name_copy);
                free(name_copy);
                return code;
            }
        }
        *result = copy_string(value);
    }

    free_values(values, value_count);
    if (!*result)
    {
        return set_system_error(error, command->span.line, command->span.column, "out of memory");
    }

    return EXEC_CODE_OK;
}

static ExecCode builtin_unset(
    ExecutorContext *context,
    const AstCommand *command,
    size_t count,
    TclError *error,
    char **result)
{
    char **values = NULL;
    size_t value_count = 0;

    if (!evaluate_words(context, command->words, count, error, &values, &value_count))
    {
        return EXEC_CODE_ERROR;
    }

    if (value_count != 2)
    {
        free_values(values, value_count);
        return set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command unset");
    }

    if (!is_supported_var_name(values[1]))
    {
        free_values(values, value_count);
        return set_semantic_error(error, command->span.line, command->span.column, "array/namespace variables are not supported");
    }

    if (!variables_unset(context->variables, values[1]))
    {
        char *name_copy = copy_string(values[1]);
        free_values(values, value_count);
        if (!name_copy)
        {
            return set_system_error(error, command->span.line, command->span.column, "out of memory");
        }
        {
            ExecCode code = set_semantic_errorf(
                error,
                command->span.line,
                command->span.column,
                "Undefined variable: %s",
                name_copy);
            free(name_copy);
            return code;
        }
    }

    free_values(values, value_count);
    *result = copy_string("");
    if (!*result)
    {
        return set_system_error(error, command->span.line, command->span.column, "out of memory");
    }

    return EXEC_CODE_OK;
}

static ExecCode builtin_incr(
    ExecutorContext *context,
    const AstCommand *command,
    size_t count,
    TclError *error,
    char **result)
{
    char **values = NULL;
    size_t value_count = 0;
    long long current;
    long long increment = 1;
    long long updated;
    char buffer[64];
    const char *existing;

    if (!evaluate_words(context, command->words, count, error, &values, &value_count))
    {
        return EXEC_CODE_ERROR;
    }

    if (value_count != 2 && value_count != 3)
    {
        free_values(values, value_count);
        return set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command incr");
    }

    if (!is_supported_var_name(values[1]))
    {
        free_values(values, value_count);
        return set_semantic_error(error, command->span.line, command->span.column, "array/namespace variables are not supported");
    }

    existing = variables_get(context->variables, values[1]);
    if (!existing)
    {
        char *name_copy = copy_string(values[1]);
        free_values(values, value_count);
        if (!name_copy)
        {
            return set_system_error(error, command->span.line, command->span.column, "out of memory");
        }
        {
            ExecCode code = set_semantic_errorf(
                error,
                command->span.line,
                command->span.column,
                "Undefined variable: %s",
                name_copy);
            free(name_copy);
            return code;
        }
    }

    if (!expr_to_longlong(existing, &current))
    {
        char message[192];
        snprintf(message, sizeof(message), "Expected integer, got \"%s\"", existing);
        free_values(values, value_count);
        return set_semantic_error(error, command->span.line, command->span.column, message);
    }

    if (value_count == 3)
    {
        if (!expr_to_longlong(values[2], &increment))
        {
            char message[192];
            snprintf(message, sizeof(message), "Expected integer, got \"%s\"", values[2]);
            free_values(values, value_count);
            return set_semantic_error(error, command->span.line, command->span.column, message);
        }
    }

    updated = current + increment;
    snprintf(buffer, sizeof(buffer), "%lld", updated);

    if (!variables_set(context->variables, values[1], buffer))
    {
        free_values(values, value_count);
        return set_system_error(error, command->span.line, command->span.column, "out of memory");
    }

    free_values(values, value_count);
    *result = copy_string(buffer);
    if (!*result)
    {
        return set_system_error(error, command->span.line, command->span.column, "out of memory");
    }

    return EXEC_CODE_OK;
}

static ExecCode builtin_puts(
    ExecutorContext *context,
    const AstCommand *command,
    size_t count,
    TclError *error,
    char **result)
{
    char **values = NULL;
    size_t value_count = 0;
    int no_newline = 0;
    FILE *stream = context->stdout_stream;
    const char *text = NULL;

    if (!evaluate_words(context, command->words, count, error, &values, &value_count))
    {
        return EXEC_CODE_ERROR;
    }

    if (value_count == 2)
    {
        text = values[1];
    }
    else if (value_count == 3 && strcmp(values[1], "-nonewline") == 0)
    {
        no_newline = 1;
        text = values[2];
    }
    else if (value_count == 3 && (strcmp(values[1], "stdout") == 0 || strcmp(values[1], "stderr") == 0))
    {
        stream = strcmp(values[1], "stderr") == 0 ? context->stderr_stream : context->stdout_stream;
        text = values[2];
    }
    else if (value_count == 4 && strcmp(values[1], "-nonewline") == 0 &&
             (strcmp(values[2], "stdout") == 0 || strcmp(values[2], "stderr") == 0))
    {
        no_newline = 1;
        stream = strcmp(values[2], "stderr") == 0 ? context->stderr_stream : context->stdout_stream;
        text = values[3];
    }
    else
    {
        free_values(values, value_count);
        return set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command puts");
    }

    if (text)
    {
        fprintf(stream, "%s", text);
        if (!no_newline)
        {
            fprintf(stream, "\n");
        }
    }

    free_values(values, value_count);
    *result = copy_string("");
    if (!*result)
    {
        return set_system_error(error, command->span.line, command->span.column, "out of memory");
    }

    return EXEC_CODE_OK;
}

static ExecCode read_gets_line(
    FILE *stream,
    char **line,
    long long *count,
    TclError *error,
    int line_no,
    int column)
{
    size_t capacity = 64;
    size_t length = 0;
    int character;
    char *buffer = (char *)malloc(capacity);

    if (!buffer)
    {
        return set_system_error(error, line_no, column, "out of memory");
    }

    while ((character = fgetc(stream)) != EOF)
    {
        if (character == '\n')
        {
            break;
        }

        if (character == '\r')
        {
            int next = fgetc(stream);
            if (next != '\n' && next != EOF)
            {
                ungetc(next, stream);
            }
            break;
        }

        if (length + 1 >= capacity)
        {
            size_t new_capacity = capacity * 2;
            char *resized = (char *)realloc(buffer, new_capacity);
            if (!resized)
            {
                free(buffer);
                return set_system_error(error, line_no, column, "out of memory");
            }

            buffer = resized;
            capacity = new_capacity;
        }

        buffer[length++] = (char)character;
    }

    if (ferror(stream))
    {
        free(buffer);
        return set_system_error(error, line_no, column, "failed to read from stdin");
    }

    buffer[length] = '\0';

    if (character == EOF && length == 0)
    {
        *count = -1;
    }
    else
    {
        *count = (long long)length;
    }

    *line = buffer;
    return EXEC_CODE_OK;
}

static ExecCode builtin_gets(
    ExecutorContext *context,
    const AstCommand *command,
    size_t count,
    TclError *error,
    char **result)
{
    char **values = NULL;
    size_t value_count = 0;
    char *line = NULL;
    long long read_count = 0;
    char count_buffer[64];
    ExecCode read_code;

    if (!evaluate_words(context, command->words, count, error, &values, &value_count))
    {
        return EXEC_CODE_ERROR;
    }

    if (value_count != 2 && value_count != 3)
    {
        free_values(values, value_count);
        return set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command gets");
    }

    if (strcmp(values[1], "stdin") != 0)
    {
        free_values(values, value_count);
        return set_semantic_error(error, command->span.line, command->span.column, "gets supports only stdin");
    }

    read_code = read_gets_line(context->stdin_stream, &line, &read_count, error, command->span.line, command->span.column);
    if (read_code != EXEC_CODE_OK)
    {
        free_values(values, value_count);
        return read_code;
    }

    if (value_count == 2)
    {
        *result = line;
    }
    else
    {
        if (!is_supported_var_name(values[2]))
        {
            free_values(values, value_count);
            free(line);
            return set_semantic_error(error, command->span.line, command->span.column, "array/namespace variables are not supported");
        }

        if (!variables_set(context->variables, values[2], line))
        {
            free_values(values, value_count);
            free(line);
            return set_system_error(error, command->span.line, command->span.column, "out of memory");
        }

        snprintf(count_buffer, sizeof(count_buffer), "%lld", read_count);
        *result = copy_string(count_buffer);
        free(line);
        if (!*result)
        {
            free_values(values, value_count);
            return set_system_error(error, command->span.line, command->span.column, "out of memory");
        }
    }

    free_values(values, value_count);
    return EXEC_CODE_OK;
}


static ExecCode execute_command(
    ExecutorContext *context,
    const AstCommand *command,
    TclError *error,
    char **result)
{
    const AstWord **words = NULL;
    char *command_name = NULL;
    size_t count = 0;
    ExecCode code = EXEC_CODE_ERROR;

    *result = NULL;

    if (!collect_words(command, &words, &count))
    {
        return set_system_error(error, command->span.line, command->span.column, "out of memory");
    }

    if (count == 0)
    {
        free((void *)words);
        *result = copy_string("");
        if (!*result)
        {
            return set_system_error(error, command->span.line, command->span.column, "out of memory");
        }
        return EXEC_CODE_OK;
    }

    if (any_expansion_word(command->words))
    {
        free((void *)words);
        return set_syntax_error(error, command->span.line, command->span.column, "argument expansion is not supported");
    }

    if (!evaluate_word(context, words[0], error, &command_name))
    {
        free((void *)words);
        return EXEC_CODE_ERROR;
    }

    if (strcmp(command_name, "set") == 0)
    {
        code = builtin_set(context, command, count, error, result);
        goto cleanup;
    }

    if (strcmp(command_name, "unset") == 0)
    {
        code = builtin_unset(context, command, count, error, result);
        goto cleanup;
    }

    if (strcmp(command_name, "incr") == 0)
    {
        code = builtin_incr(context, command, count, error, result);
        goto cleanup;
    }

    if (strcmp(command_name, "puts") == 0)
    {
        code = builtin_puts(context, command, count, error, result);
        goto cleanup;
    }

    if (strcmp(command_name, "gets") == 0)
    {
        code = builtin_gets(context, command, count, error, result);
        goto cleanup;
    }

    if (strcmp(command_name, "if") == 0)
    {
        code = execute_if_command(context, words, count, error, result);
        goto cleanup;
    }

    if (strcmp(command_name, "while") == 0)
    {
        const AstWord *condition_word;
        const AstWord *body_word;

        if (count != 3)
        {
            code = set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command while");
            goto cleanup;
        }

        condition_word = words[1];
        body_word = words[2];

        context->loop_depth++;
        while (1)
        {
            int is_true = 0;
            char *body_result = NULL;
            ExecCode cond_code = evaluate_condition(context, condition_word, error, &is_true);

            if (cond_code != EXEC_CODE_OK)
            {
                code = cond_code;
                break;
            }

            if (!is_true)
            {
                code = EXEC_CODE_OK;
                break;
            }

            code = execute_script_text_with_code(context, body_word->text, body_word->span.line, body_word->span.column, error, &body_result);
            free(body_result);

            if (code == EXEC_CODE_OK)
            {
                continue;
            }
            if (code == EXEC_CODE_CONTINUE)
            {
                continue;
            }
            if (code == EXEC_CODE_BREAK)
            {
                code = EXEC_CODE_OK;
                break;
            }
            break;
        }
        context->loop_depth--;

        if (code == EXEC_CODE_OK)
        {
            *result = copy_string("");
            if (!*result)
            {
                code = set_system_error(error, command->span.line, command->span.column, "out of memory");
            }
        }

        goto cleanup;
    }

    if (strcmp(command_name, "for") == 0)
    {
        const AstWord *start_word;
        const AstWord *test_word;
        const AstWord *next_word;
        const AstWord *body_word;
        char *start_result = NULL;

        if (count != 5)
        {
            code = set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command for");
            goto cleanup;
        }

        start_word = words[1];
        test_word = words[2];
        next_word = words[3];
        body_word = words[4];

        code = execute_script_text_with_code(context, start_word->text, start_word->span.line, start_word->span.column, error, &start_result);
        free(start_result);
        if (code != EXEC_CODE_OK)
        {
            goto cleanup;
        }

        context->loop_depth++;
        while (1)
        {
            int is_true = 0;
            char *body_result = NULL;
            ExecCode cond_code = evaluate_condition(context, test_word, error, &is_true);

            if (cond_code != EXEC_CODE_OK)
            {
                code = cond_code;
                break;
            }

            if (!is_true)
            {
                code = EXEC_CODE_OK;
                break;
            }

            code = execute_script_text_with_code(context, body_word->text, body_word->span.line, body_word->span.column, error, &body_result);
            free(body_result);

            if (code == EXEC_CODE_BREAK)
            {
                code = EXEC_CODE_OK;
                break;
            }
            if (code != EXEC_CODE_OK && code != EXEC_CODE_CONTINUE)
            {
                break;
            }

            {
                char *next_result = NULL;
                ExecCode next_code = execute_script_text_with_code(context, next_word->text, next_word->span.line, next_word->span.column, error, &next_result);
                free(next_result);

                if (next_code == EXEC_CODE_BREAK)
                {
                    code = EXEC_CODE_OK;
                    break;
                }
                if (next_code != EXEC_CODE_OK && next_code != EXEC_CODE_CONTINUE)
                {
                    code = next_code;
                    break;
                }
            }
        }
        context->loop_depth--;

        if (code == EXEC_CODE_OK)
        {
            *result = copy_string("");
            if (!*result)
            {
                code = set_system_error(error, command->span.line, command->span.column, "out of memory");
            }
        }

        goto cleanup;
    }

    if (strcmp(command_name, "break") == 0)
    {
        if (count != 1)
        {
            code = set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command break");
            goto cleanup;
        }

        if (context->loop_depth <= 0)
        {
            code = set_semantic_error(error, command->span.line, command->span.column, "break outside loop");
            goto cleanup;
        }

        *result = copy_string("");
        if (!*result)
        {
            code = set_system_error(error, command->span.line, command->span.column, "out of memory");
            goto cleanup;
        }

        code = EXEC_CODE_BREAK;
        goto cleanup;
    }

    if (strcmp(command_name, "continue") == 0)
    {
        if (count != 1)
        {
            code = set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command continue");
            goto cleanup;
        }

        if (context->loop_depth <= 0)
        {
            code = set_semantic_error(error, command->span.line, command->span.column, "continue outside loop");
            goto cleanup;
        }

        *result = copy_string("");
        if (!*result)
        {
            code = set_system_error(error, command->span.line, command->span.column, "out of memory");
            goto cleanup;
        }

        code = EXEC_CODE_CONTINUE;
        goto cleanup;
    }

    if (strcmp(command_name, "return") == 0)
    {
        char **values = NULL;
        size_t value_count = 0;
        const char *return_value = "";

        if (context->proc_depth <= 0)
        {
            code = set_semantic_error(error, command->span.line, command->span.column, "return outside procedure");
            goto cleanup;
        }

        if (!evaluate_words(context, command->words, count, error, &values, &value_count))
        {
            code = EXEC_CODE_ERROR;
            goto cleanup;
        }

        if (value_count != 1 && value_count != 2)
        {
            free_values(values, value_count);
            code = set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command return");
            goto cleanup;
        }

        if (value_count == 2)
        {
            return_value = values[1];
        }

        *result = copy_string(return_value);
        free_values(values, value_count);
        if (!*result)
        {
            code = set_system_error(error, command->span.line, command->span.column, "out of memory");
            goto cleanup;
        }

        code = EXEC_CODE_RETURN;
        goto cleanup;
    }

    if (strcmp(command_name, "proc") == 0)
    {
        char **values = NULL;
        size_t value_count = 0;
        size_t arg_count = 0;

        if (!evaluate_words(context, command->words, count, error, &values, &value_count))
        {
            code = EXEC_CODE_ERROR;
            goto cleanup;
        }

        if (value_count != 4)
        {
            free_values(values, value_count);
            code = set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command proc");
            goto cleanup;
        }

        if (!is_supported_var_name(values[1]))
        {
            free_values(values, value_count);
            code = set_semantic_error(error, command->span.line, command->span.column, "invalid proc name");
            goto cleanup;
        }

        if (!count_proc_args(values[2], &arg_count))
        {
            free_values(values, value_count);
            code = set_syntax_error(error, command->span.line, command->span.column, "invalid proc argument list");
            goto cleanup;
        }

        if (!procedure_add_or_update(&context->procedures, values[1], values[2], values[3], arg_count))
        {
            free_values(values, value_count);
            code = set_system_error(error, command->span.line, command->span.column, "out of memory");
            goto cleanup;
        }

        free_values(values, value_count);
        *result = copy_string("");
        if (!*result)
        {
            code = set_system_error(error, command->span.line, command->span.column, "out of memory");
            goto cleanup;
        }

        code = EXEC_CODE_OK;
        goto cleanup;
    }

    if (strcmp(command_name, "expr") == 0)
    {
        char **values = NULL;
        size_t value_count = 0;
        size_t arg_index;
        size_t total_length = 0;
        size_t pos = 0;
        char *expr_text;

        if (!evaluate_words(context, command->words, count, error, &values, &value_count))
        {
            code = EXEC_CODE_ERROR;
            goto cleanup;
        }

        if (value_count < 2)
        {
            free_values(values, value_count);
            code = set_syntax_error(error, command->span.line, command->span.column, "Wrong number of arguments for command expr");
            goto cleanup;
        }

        for (arg_index = 1; arg_index < value_count; arg_index++)
        {
            total_length += strlen(values[arg_index]);
            if (arg_index + 1 < value_count)
            {
                total_length++;
            }
        }

        expr_text = (char *)malloc(total_length + 1);
        if (!expr_text)
        {
            free_values(values, value_count);
            code = set_system_error(error, command->span.line, command->span.column, "out of memory");
            goto cleanup;
        }

        for (arg_index = 1; arg_index < value_count; arg_index++)
        {
            size_t length = strlen(values[arg_index]);
            memcpy(expr_text + pos, values[arg_index], length);
            pos += length;
            if (arg_index + 1 < value_count)
            {
                expr_text[pos++] = ' ';
            }
        }
        expr_text[pos] = '\0';

        code = evaluate_expression(context, expr_text, command->span.line, command->span.column, error, result)
                   ? EXEC_CODE_OK
                   : EXEC_CODE_ERROR;

        free(expr_text);
        free_values(values, value_count);
        goto cleanup;
    }

    {
        Procedure *proc = procedure_find(context->procedures, command_name);
        if (proc)
        {
            char **values = NULL;
            size_t value_count = 0;

            if (!evaluate_words(context, command->words, count, error, &values, &value_count))
            {
                code = EXEC_CODE_ERROR;
                goto cleanup;
            }

            code = execute_procedure_call(
                context,
                proc,
                values + 1,
                value_count - 1,
                command->span.line,
                command->span.column,
                error,
                result);
            free_values(values, value_count);
            goto cleanup;
        }
    }

    code = set_semantic_errorf(error, command->span.line, command->span.column, "Unknown command: %s", command_name);

cleanup:
    free(command_name);
    free((void *)words);
    return code;
}

int executor_execute(ExecutorContext *context, const AstCommand *program, TclError *error)
{
    char *result = NULL;
    ExecCode code = executor_execute_program(context, program, error, &result);

    if (code != EXEC_CODE_OK)
    {
        free(result);
        return 0;
    }

    free(context->result);
    context->result = result;
    return 1;
}
