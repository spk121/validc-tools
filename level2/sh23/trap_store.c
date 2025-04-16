#include "trap_store.h"
#include <stdlib.h>

struct TrapStore {
    TrapArray *traps;
};

// Comparison function for finding Trap by signal
static int compare_trap_signal(const Trap *trap, const void *signal)
{
    return trap_get_signal(trap) - *(const int *)signal;
}

// Constructor
TrapStore *trap_store_create(void)
{
    TrapStore *store = malloc(sizeof(TrapStore));
    if (!store) {
        log_fatal("trap_store_create: out of memory");
        return NULL;
    }

    store->traps = trap_array_create_with_free((TrapArrayFreeFunc)trap_destroy);
    if (!store->traps) {
        free(store);
        log_fatal("trap_store_create: failed to create traps array");
        return NULL;
    }

    return store;
}

// Destructor
void trap_store_destroy(TrapStore *store)
{
    if (store) {
        log_debug("trap_store_destroy: freeing store %p, traps %zu",
                  store, trap_array_size(store->traps));
        trap_array_destroy(store->traps);
        free(store);
    }
}

// Clear all traps
int trap_store_clear(TrapStore *store)
{
    return_val_if_null(store, -1);

    log_debug("trap_store_clear: clearing store %p, traps %zu",
              store, trap_array_size(store->traps));

    return trap_array_clear(store->traps);
}

// Trap management
int trap_store_set_trap(TrapStore *store, int signal, const String *action)
{
    return_val_if_null(store, -1);

    size_t index;
    if (trap_array_find_with_compare(store->traps, &signal, compare_trap_signal, &index) == 0) {
        // Replace existing trap
        Trap *new_trap = trap_create(signal, action);
        if (!new_trap) {
            log_fatal("trap_store_set_trap: failed to create trap for signal %d", signal);
            return -1;
        }
        return trap_array_set(store->traps, index, new_trap);
    }

    // Add new trap
    Trap *trap = trap_create(signal, action);
    if (!trap) {
        log_fatal("trap_store_set_trap: failed to create trap for signal %d", signal);
        return -1;
    }

    if (trap_array_append(store->traps, trap) != 0) {
        trap_destroy(trap);
        log_fatal("trap_store_set_trap: failed to append trap for signal %d", signal);
        return -1;
    }

    return 0;
}

int trap_store_set_trap_cstr(TrapStore *store, int signal, const char *action)
{
    return_val_if_null(store, -1);

    size_t index;
    if (trap_array_find_with_compare(store->traps, &signal, compare_trap_signal, &index) == 0) {
        // Replace existing trap
        Trap *new_trap = trap_create_from_cstr(signal, action);
        if (!new_trap) {
            log_fatal("trap_store_set_trap_cstr: failed to create trap for signal %d", signal);
            return -1;
        }
        return trap_array_set(store->traps, index, new_trap);
    }

    // Add new trap
    Trap *trap = trap_create_from_cstr(signal, action);
    if (!trap) {
        log_fatal("trap_store_set_trap_cstr: failed to create trap for signal %d", signal);
        return -1;
    }

    if (trap_array_append(store->traps, trap) != 0) {
        trap_destroy(trap);
        log_fatal("trap_store_set_trap_cstr: failed to append trap for signal %d", signal);
        return -1;
    }

    return 0;
}

int trap_store_remove_trap(TrapStore *store, int signal)
{
    return_val_if_null(store, -1);

    size_t index;
    if (trap_array_find_with_compare(store->traps, &signal, compare_trap_signal, &index) != 0) {
        return -1; // Signal not found
    }

    return trap_array_remove(store->traps, index);
}

const Trap *trap_store_get_trap(const TrapStore *store, int signal)
{
    return_val_if_null(store, NULL);

    size_t index;
    if (trap_array_find_with_compare(store->traps, &signal, compare_trap_signal, &index) != 0) {
        return NULL; // Signal not found
    }

    return trap_array_get(store->traps, index);
}

const String *trap_store_get_action(const TrapStore *store, int signal)
{
    const Trap *trap = trap_store_get_trap(store, signal);
    return trap ? trap_get_action(trap) : NULL;
}

const char *trap_store_get_action_cstr(const TrapStore *store, int signal)
{
    const Trap *trap = trap_store_get_trap(store, signal);
    return trap ? trap_get_action_cstr(trap) : NULL;
}

int trap_store_has_trap(const TrapStore *store, int signal)
{
    return_val_if_null(store, -1);

    size_t index;
    return trap_array_find_with_compare(store->traps, &signal, compare_trap_signal, &index) == 0 ? 1 : 0;
}
