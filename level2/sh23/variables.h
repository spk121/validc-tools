#ifndef VARIABLES_H
#define VARIABLES_H

void set_variable(const char *name, const char *value);
void export_variable(const char *name);
void unset_variable(const char *name);
const char *get_variable(const char *name);
void make_readonly(const char *name);
void dump_variables(void);

#endif
