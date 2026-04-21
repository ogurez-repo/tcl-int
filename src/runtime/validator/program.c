#include "runtime/validator/internal.h"

int validator_validate_program(ValidatorContext *context, const AstCommand *program, TclError *error)
{
    const AstCommand *command;

    if (!validator_collect_proc_definitions(context, program, error))
    {
        return 0;
    }

    command = program;
    while (command)
    {
        if (!validator_validate_command(context, command, error))
        {
            return 0;
        }
        command = command->next;
    }

    return 1;
}
