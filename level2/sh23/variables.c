#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "variables.h"
#include "execute.h"

#define MAX_VARS 100
#define MAX_NAME 32
#define MAX_VALUE 256

struct Variable {
    char name[MAX_NAME];
    char value[MAX_VALUE];
    int exported;
};
static struct Variable vars[MAX_VARS];
static int var_count = 0;
static char status_str[16]; // Buffer for $? string

static int find_var(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) return i;
    }
    return -1;
}

void set_variable(const char *name, const char *value) {
    int idx = find_var(name);
    if (idx == -1 && var_count < MAX_VARS) {
        idx = var_count++;
        strncpy(vars[idx].name, name, MAX_NAME - 1);
        vars[idx].name[MAX_NAME - 1] = '\0';
        vars[idx].exported = 0;
    }
    if (idx != -1) {
        strncpy(vars[idx].value, value, MAX_VALUE - 1);
        vars[idx].value[MAX_VALUE - 1] = '\0';
    }
}

void export_variable(const char *name) {
    int idx = find_var(name);
    if (idx != -1 && !vars[idx].exported) {
        vars[idx].exported = 1;
        char env[MAX_NAME + MAX_VALUE + 1];
        snprintf(env, sizeof(env), "%s=%s", vars[idx].name, vars[idx].value);
        if (putenv(strdup(env)) != 0) {
            perror("putenv failed");
        }
    }
}

void unset_variable(const char *name) {
    int idx = find_var(name);
    if (idx != -1) {
        if (vars[idx].exported) {
            char env[MAX_NAME + 1];
            snprintf(env, sizeof(env), "%s=", name);
            if (putenv(strdup(env)) != 0) {
                perror("putenv failed");
            }
        }
        for (int i = idx; i < var_count - 1; i++) {
            vars[i] = vars[i + 1];
        }
        var_count--;
    }
}

const char *get_variable(const char *name) {
    if (strcmp(name, "?") == 0) {
        snprintf(status_str, sizeof(status_str), "%d", get_last_status());
        return status_str;
    }
    int idx = find_var(name);
    if (idx != -1) return vars[idx].value;
    return "";
}

void make_readonly(const char *name) {
    (void)name; // Stub
}

void dump_variables(void) {
    for (int i = 0; i < var_count; i++) {
        printf("%s=%s (exported: %d)\n", vars[i].name, vars[i].value, vars[i].exported);
    }
}
