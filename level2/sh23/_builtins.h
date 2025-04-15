#ifndef BUILTINS_H
#define BUILTINS_H

#include "executor.h"
#include "trap_store.h"

// Execute a builtin command
ExecStatus builtin_execute(Executor *exec, char **argv, int argc);

#endif
