#ifndef TRAP_STORE_H
#define TRAP_STORE_H

#include "string.h"
#include "ptr_array.h"
#include "executor.h"

typedef struct {
    int signal;      // Signal number (e.g., SIGINT)
    String *action;  // Command string (NULL for default, "-" for ignore)
} Trap;

typedef struct {
    PtrArray *traps; // Array of Trap*
} TrapStore;

// Create and destroy trap store
TrapStore *trap_store_create(void);
void trap_store_destroy(TrapStore *store);

// Set trap action for a signal
void trap_store_set(TrapStore *store, int signal, const char *action);

// Get trap for a signal
Trap *trap_store_get(TrapStore *store, int signal);

// Print all traps (for 'trap' command)
void trap_store_print(TrapStore *store);

// Set executor for signal handler
void trap_store_set_executor(Executor *exec);

#endif
