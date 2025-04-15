#ifndef VARIABLES_H
#define VARIABLES_H

#include <stdbool.h>

typedef struct {
    char *name;
    char *value;
    bool exported; // 1 if exported, 0 otherwise
    bool read_only;
} Variable;

typedef struct {
    Variable *variables;
    int var_count;
    int var_capacity;
    char status_str[16];  // Store for $?
} VariableStore;

void variable_store_set_variable(VariableStore *self, const char *name, const char *value);
void variable_store_export_variable(VariableStore *self, const char *name);
void variable_store_unset_variable(VariableStore *self, const char *name);
const char *variable_store_get_variable(VariableStore *self, const char *name);
void variable_store_make_readonly(VariableStore *self, const char *name);
void variable_store_dump_variables(VariableStore *self);

#endif
