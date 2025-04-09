#ifndef EXECUTE_H
#define EXECUTE_H

void expand_command(const char *cmd, char *out);
int execute_command(const char *cmd);
void execute_script(const char *filename);
int get_last_status(void);

#endif
