#ifndef CONTROL_H
#define CONTROL_H

void execute_if(const char *condition, const char *then_block);
void execute_case(const char *word, const char *patterns);
void execute_for(const char *var, const char *list, const char *body);
void execute_while(const char *condition, const char *body);
void execute_until(const char *condition, const char *body);

#endif
