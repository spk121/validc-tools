#ifndef TRAP_STORE_H
#define TRAP_STORE_H

#include "string.h"
#include "trap_array.h"
#include "logging.h"

typedef struct TrapStore TrapStore;

// Constructor
TrapStore *trap_store_create(void);

// Destructor
void trap_store_destroy(TrapStore *store);

// Clear all traps
int trap_store_clear(TrapStore *store);

// Trap management
int trap_store_set_trap(TrapStore *store, int signal, const String *action);
int trap_store_set_trap_cstr(TrapStore *store, int signal, const char *action);
int trap_store_remove_trap(TrapStore *store, int signal);
const Trap *trap_store_get_trap(const TrapStore *store, int signal);
const String *trap_store_get_action(const TrapStore *store, int signal);
const char *trap_store_get_action_cstr(const TrapStore *store, int signal);
int trap_store_has_trap(const TrapStore *store, int signal);

#endif
