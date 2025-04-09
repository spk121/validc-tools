#ifndef BUILTINS_H
#define BUILTINS_H

#define MAX_CMD 1024 // Define here for echo

void builtin_colon(void);
void builtin_break(void);
void builtin_continue(void);
void builtin_exit(int status);
void builtin_export(const char *name);
void builtin_readonly(const char *name);
void builtin_set(int argc, char **argv);
void builtin_shift(int n);
void builtin_unset(const char *name);
void builtin_alias(const char *name, const char *value);
void builtin_unalias(const char *name);
void builtin_getopts(void);
void builtin_read(const char *varname);
void builtin_dot(const char *filename);
void builtin_eval(const char *string);
void builtin_command(const char *cmd);
void builtin_test(const char *expr);
void builtin_type(const char *name);
void builtin_echo(const char *args);
void builtin_showvars(void);
int substitute_alias(char *token, char *output, int max_len);

#endif
