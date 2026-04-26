#include <string.h>

#include "core/ast.h"
#include "core/errors.h"
#include "runtime/validator/internal.h"

int validator_validate_if_words(ValidatorContext *context, const AstWord **words, int count, TclError *error)
{
    int index = 1;

    if (count < 3)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, words[0]->span.line, words[0]->span.column, "if expects condition and body");
        return 0;
    }

    while (1)
    {
        const AstWord *condition;
        const AstWord *body;

        if (index >= count)
        {
            return validator_syntax_error_at_word(error, words[count - 1], "if missing condition after elseif");
        }

        condition = words[index++];
        if (!validator_validate_expression_text(
                context,
                condition->text,
                condition->span.line,
                condition->span.column,
                error))
        {
            return 0;
        }

        if (index < count && validator_word_is_literal_keyword(words[index], "then"))
        {
            index++;
        }

        if (index >= count)
        {
            return validator_syntax_error_at_word(error, words[count - 1], "if missing body");
        }

        body = words[index++];
        if (!validator_validate_script_text(
                context,
                body->text,
                body->span.line,
                body->span.column + 1,
                error))
        {
            return 0;
        }

        if (index >= count)
        {
            return 1;
        }

        if (validator_word_is_literal_keyword(words[index], "elseif"))
        {
            index++;
            continue;
        }

        if (validator_word_is_literal_keyword(words[index], "else"))
        {
            index++;
            if (index >= count)
            {
                return validator_syntax_error_at_word(error, words[count - 1], "if missing else body");
            }

            body = words[index++];
            if (!validator_validate_script_text(
                    context,
                    body->text,
                    body->span.line,
                    body->span.column + 1,
                    error))
            {
                return 0;
            }

            if (index != count)
            {
                return validator_syntax_error_at_word(error, words[index], "unexpected token after else body");
            }

            return 1;
        }

        return validator_syntax_error_at_word(error, words[index], "expected elseif or else");
    }
}
