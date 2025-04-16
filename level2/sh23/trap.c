#include "trap.h"
#include <stdlib.h>

struct Trap {
    int signal;
    String *action;
};

// Constructors
Trap *trap_create(int signal, const String *action)
{
    Trap *trap = malloc(sizeof(Trap));
    if (!trap) {
        log_fatal("trap_create: out of memory");
        return NULL;
    }

    trap->signal = signal;
    trap->action = action ? string_create_from((String *)action) : NULL;
    if (action && !trap->action) {
        free(trap);
        log_fatal("trap_create: failed to create action");
        return NULL;
    }

    return trap;
}

Trap *trap_create_from_cstr(int signal, const char *action)
{
    Trap *trap = malloc(sizeof(Trap));
    if (!trap) {
        log_fatal("trap_create_from_cstr: out of memory");
        return NULL;
    }

    trap->signal = signal;
    trap->action = action ? string_create_from_cstr(action) : NULL;
    if (action && !trap->action) {
        free(trap);
        log_fatal("trap_create_from_cstr: failed to create action");
        return NULL;
    }

    return trap;
}

// Destructor
void trap_destroy(Trap *trap)
{
    if (trap) {
        log_debug("trap_destroy: freeing trap %p, signal = %d, action = %s",
                  trap,
                  trap->signal,
                  trap->action ? string_data(trap->action) : "(null)");
        string_destroy(trap->action);
        free(trap);
    }
}

// Getters
int trap_get_signal(const Trap *trap)
{
    return_val_if_null(trap, -1);
    return trap->signal;
}

const String *trap_get_action(const Trap *trap)
{
    return_val_if_null(trap, NULL);
    return trap->action;
}

const char *trap_get_action_cstr(const Trap *trap)
{
    return_val_if_null(trap, NULL);
    return trap->action ? string_data(trap->action) : NULL;
}

// Setters
int trap_set_signal(Trap *trap, int signal)
{
    return_val_if_null(trap, -1);
    trap->signal = signal;
    return 0;
}

int trap_set_action(Trap *trap, const String *action)
{
    return_val_if_null(trap, -1);

    String *new_action = action ? string_create_from((String *)action) : NULL;
    if (action && !new_action) {
        log_fatal("trap_set_action: failed to create action");
        return -1;
    }

    string_destroy(trap->action);
    trap->action = new_action;
    return 0;
}

int trap_set_action_cstr(Trap *trap, const char *action)
{
    return_val_if_null(trap, -1);

    String *new_action = action ? string_create_from_cstr(action) : NULL;
    if (action && !new_action) {
        log_fatal("trap_set_action_cstr: failed to create action");
        return -1;
    }

    string_destroy(trap->action);
    trap->action = new_action;
    return 0;
}
