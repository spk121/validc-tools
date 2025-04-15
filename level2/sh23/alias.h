#ifndef ALIAS_H
#define ALIAS_H

#define MAX_ALIAS_NAME 256
#define MAX_ALIAS_VALUE 1024

typedef struct
{
    char name[MAX_ALIAS_NAME];
    char value[MAX_ALIAS_VALUE];
} Alias;

// Opaque handle for alias store
typedef struct AliasStore AliasStore;

AliasStore *alias_store_create(void);
void alias_store_destroy(AliasStore *store);
int alias_add(AliasStore *store, const char *name, const char *value);
int alias_remove(AliasStore *store, const char *name);
const char *alias_get(const AliasStore *store, const char *name);
int alias_exists(const AliasStore *store, const char *name);
int alias_is_active(const char *name, char **active_aliases, int active_alias_count);

#endif
