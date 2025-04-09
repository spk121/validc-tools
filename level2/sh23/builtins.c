#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "builtins.h"
#include "variables.h"

#define MAX_CMD 1024
#define MAX_ALIASES 100
#define MAX_ALIAS_NAME 32
#define MAX_ALIAS_VALUE 256

struct Alias {
    char name[MAX_ALIAS_NAME];
    char value[MAX_ALIAS_VALUE];
};
static struct Alias aliases[MAX_ALIASES];
static int alias_count = 0;

static int find_alias(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) return i;
    }
    return -1;
}

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

void builtin_alias(const char *name, const char *value) {
    if (!value || !*value) { // List aliases if no value
        for (int i = 0; i < alias_count; i++) {
            printf("alias %s='%s'\n", aliases[i].name, aliases[i].value);
        }
        return;
    }
    int idx = find_alias(name);
    if (idx == -1 && alias_count < MAX_ALIASES) {
        idx = alias_count++;
        strncpy(aliases[idx].name, name, MAX_ALIAS_NAME - 1);
        aliases[idx].name[MAX_ALIAS_NAME - 1] = '\0';
    }
    if (idx != -1) {
        strncpy(aliases[idx].value, value, MAX_ALIAS_VALUE - 1);
        aliases[idx].value[MAX_ALIAS_VALUE - 1] = '\0';
    }
}

void builtin_unalias(const char *name) {
    int idx = find_alias(name);
    if (idx != -1) {
        for (int i = idx; i < alias_count - 1; i++) {
            aliases[i] = aliases[i + 1];
        }
        alias_count--;
    }
}

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
        while (*arg == ' ') arg++;
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

// Function to check and substitute aliases (exposed for parser)
int substitute_alias(char *token, char *output, int max_len) {
    int idx = find_alias(token);
    if (idx != -1) {
        strncpy(output, aliases[idx].value, max_len - 1);
        output[max_len - 1] = '\0';
        return 1;
    }
    return 0;
}
