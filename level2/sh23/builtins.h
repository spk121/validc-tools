#ifndef BUILTINS_H
#define BUILTINS_H

#include "executor.h"
#include "trap_store.h"

// Check if a command is a special builtin
bool is_special_builtin(const char *name);

// Execute a builtin command
ExecStatus builtin_execute(Executor *exec, char **argv, int argc);

#endif
