#ifndef ALIAS_H
#define ALIAS_H

#include "String.h"

typedef struct
{
    String *name;
    String *value;
} Alias;

#if 0
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

#endif
