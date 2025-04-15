#include "alias.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_CAPACITY 16

struct AliasStore
{
    Alias *aliases;
    int count;
    int capacity;
};

AliasStore *alias_store_create(void)
{
    AliasStore *store = malloc(sizeof(AliasStore));
    if (!store)
        return NULL;
    store->aliases = malloc(INITIAL_CAPACITY * sizeof(Alias));
    if (!store->aliases)
    {
        free(store);
        return NULL;
    }
    store->count = 0;
    store->capacity = INITIAL_CAPACITY;
    return store;
}

void alias_store_destroy(AliasStore *store)
{
    if (store)
    {
        free(store->aliases);
        free(store);
    }
}

static int is_valid_alias_name(const char *name)
{
    if (!name || !*name || isdigit(*name))
        return 0;
    for (const char *p = name; *p; p++)
    {
        if (!isalnum(*p) && *p != '_')
            return 0;
    }
    return 1;
}

int alias_add(AliasStore *store, const char *name, const char *value)
{
    if (!store || !name || !value || !is_valid_alias_name(name))
        return -1;

    // Check if alias already exists
    for (int i = 0; i < store->count; i++)
    {
        if (strcmp(store->aliases[i].name, name) == 0)
        {
            // Update existing alias
            strncpy(store->aliases[i].value, value, MAX_ALIAS_VALUE - 1);
            store->aliases[i].value[MAX_ALIAS_VALUE - 1] = '\0';
            return 0;
        }
    }

    // Resize if needed
    if (store->count >= store->capacity)
    {
        int new_capacity = store->capacity * 2;
        Alias *new_aliases = realloc(store->aliases, new_capacity * sizeof(Alias));
        if (!new_aliases)
            return -1;
        store->aliases = new_aliases;
        store->capacity = new_capacity;
    }

    // Add new alias
    strncpy(store->aliases[store->count].name, name, MAX_ALIAS_NAME - 1);
    store->aliases[store->count].name[MAX_ALIAS_NAME - 1] = '\0';
    strncpy(store->aliases[store->count].value, value, MAX_ALIAS_VALUE - 1);
    store->aliases[store->count].value[MAX_ALIAS_VALUE - 1] = '\0';
    store->count++;
    return 0;
}

int alias_remove(AliasStore *store, const char *name)
{
    if (!store || !name)
        return -1;
    for (int i = 0; i < store->count; i++)
    {
        if (strcmp(store->aliases[i].name, name) == 0)
        {
            // Shift remaining aliases
            for (int j = i; j < store->count - 1; j++)
            {
                store->aliases[j] = store->aliases[j + 1];
            }
            store->count--;
            return 0;
        }
    }
    return -1; // Not found
}

const char *alias_get(const AliasStore *store, const char *name)
{
    if (!store || !name)
        return NULL;
    for (int i = 0; i < store->count; i++)
    {
        if (strcmp(store->aliases[i].name, name) == 0)
        {
            return store->aliases[i].value;
        }
    }
    return NULL;
}

int alias_exists(const AliasStore *store, const char *name)
{
    return alias_get(store, name) != NULL;
}

int alias_is_active(const char *name, char **active_aliases, int active_alias_count)
{
    if (!name || !active_aliases)
        return 0;
    for (int i = 0; i < active_alias_count; i++)
    {
        if (active_aliases[i] && strcmp(active_aliases[i], name) == 0)
        {
            return 1;
        }
    }
    return 0;
}
