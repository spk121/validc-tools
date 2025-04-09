#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "builtins.h"
#include "variables.h"

#define MAX_CMD 1024 // Define here for echo

void builtin_colon(void) {}
void builtin_break(void) {}
void builtin_continue(void) {}
void builtin_exit(int status) {
    exit(status);
}
void builtin_export(const char *name) {
    export_variable(name);
}
void builtin_readonly(const char *name) { (void)name; }
void builtin_set(int argc, char **argv) { (void)argc; (void)argv; }
void builtin_shift(int n) { (void)n; }
void builtin_unset(const char *name) {
    unset_variable(name);
}
void builtin_alias(const char *name, const char *value) { (void)name; (void)value; }
void builtin_getopts(void) {}
void builtin_read(const char *varname) { (void)varname; }
void builtin_dot(const char *filename) { (void)filename; }
void builtin_eval(const char *string) { (void)string; }
void builtin_command(const char *cmd) { (void)cmd; }
void builtin_test(const char *expr) { (void)expr; }
void builtin_type(const char *name) { (void)name; }

void builtin_echo(const char *args) {
    if (!args || !*args) {
        printf("\n");
        return;
    }
    char *arg = (char *)args;
    int first = 1;
    while (*arg) {
        while (*arg == ' ') arg++; // Skip leading spaces
        if (!*arg) break;
        char word[MAX_CMD];
        int i = 0;
        while (*arg && *arg != ' ' && i < MAX_CMD - 1) {
            word[i++] = *arg++;
        }
        word[i] = '\0';
        if (!first) printf(" ");
        printf("%s", word);
        first = 0;
    }
    printf("\n");
}

void builtin_showvars(void) {
    dump_variables();
}
