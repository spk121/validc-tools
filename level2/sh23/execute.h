#ifndef EXECUTE_H
#define EXECUTE_H

#include "variables.h"

void expand_command(VariableStore *var_store, const char *cmd, char *out);
int execute_command(VariableStore *var_store, const char *cmd);
void execute_script(const char *filename);
int get_last_status(void);

#endif
