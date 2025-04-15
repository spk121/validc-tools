#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <sys/types.h>
#include "_parser.h"
#include "_variables.h"
#include "_function_store.h"
#include "string.h"
#include "ptr_array.h"

typedef struct {
    int signal;      // Signal number (e.g., SIGINT)
    String *action;  // Command string (NULL for default, "-" for ignore)
} Trap;

typedef struct {
    PtrArray *traps; // Array of Trap*
} TrapStore;

typedef struct {
    VariableStore *vars;      // Variable storage
    Tokenizer *tokenizer;     // For command substitutions
    AliasStore *alias_store;  // For alias expansion
    FunctionStore *func_store; // Function definitions
    TrapStore *trap_store;    // Signal traps
    int last_status;          // $? equivalent
    pid_t last_bg_pid;        // $! equivalent
    int pipe_fds[2];          // For pipeline management
    int saved_fds[3];         // Save stdin/stdout/stderr
    int in_subshell;          // Track subshell context
    int break_count;          // For loop control
    int continue_count;       // For loop control
    char *function_name;      // Current function, if any
    int loop_depth;           // Track nested loop depth
    int function_depth;       // Track nested function calls
} Executor;

typedef enum {
    EXEC_SUCCESS,
    EXEC_FAILURE,
    EXEC_BREAK,
    EXEC_CONTINUE,
    EXEC_RETURN
} ExecStatus;

// Create and destroy executor
Executor *executor_create(VariableStore *vars, Tokenizer *tokenizer, AliasStore *alias_store, FunctionStore *func_store);
void executor_destroy(Executor *exec);

// Execute AST
ExecStatus executor_run(Executor *exec, ASTNode *ast);

// Utility functions
void executor_set_status(Executor *exec, int status);
int executor_get_status(Executor *exec);

#endif
