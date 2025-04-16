#ifndef ALIAS_H
#define ALIAS_H

#include "string.h"
#include "logging.h"

typedef struct Alias Alias;

// Constructors
Alias *alias_create(const String *name, const String *value);
Alias *alias_create_from_cstr(const char *name, const char *value);

// Destructor
void alias_destroy(Alias *alias);

// Getters
const String *alias_get_name(const Alias *alias);
const String *alias_get_value(const Alias *alias);
const char *alias_get_name_cstr(const Alias *alias);
const char *alias_get_value_cstr(const Alias *alias);

// Setters
int alias_set_name(Alias *alias, const String *name);
int alias_set_value(Alias *alias, const String *value);
int alias_set_name_cstr(Alias *alias, const char *name);
int alias_set_value_cstr(Alias *alias, const char *value);

#endif
