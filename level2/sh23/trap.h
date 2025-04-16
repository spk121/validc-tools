#ifndef TRAP_H
#define TRAP_H

#include <stdbool.h>
#include "string.h"
#include "logging.h"

typedef struct Trap Trap;

// Constructors
Trap *trap_create(int signal, const String *action);
Trap *trap_create_from_cstr(int signal, const char *action);

// Destructor
void trap_destroy(Trap *trap);

// Getters
int trap_get_signal(const Trap *trap);
const String *trap_get_action(const Trap *trap);
const char *trap_get_action_cstr(const Trap *trap);

// Setters
int trap_set_signal(Trap *trap, int signal);
int trap_set_action(Trap *trap, const String *action);
int trap_set_action_cstr(Trap *trap, const char *action);

#endif
