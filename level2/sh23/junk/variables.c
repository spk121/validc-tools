#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "variables.h"
#include "execute.h"
#include "logging.h"
#include "c23lib.h"

#define MAX_VARS 100
#define MAX_NAME 32
#define MAX_VALUE 256

static int find_var(const VariableStore *self, const char *name) {
    for (int i = 0; i < self->var_count; i++) {
        if (strcmp(self->variables[i].name, name) == 0) return i;
    }
    return -1;
}

void variable_store_set_variable(VariableStore *self, const char *name, const char *value)
{
    return_if_null (self);
    return_if_null (name);
    return_if_eq (strlen(name), 0);
    return_if_gt (strlen(name), MAX_NAME);
    return_if_null (value);
    return_if_eq (strlen(value), 0);
    return_if_gt (strlen(value), MAX_VALUE);

    int idx = find_var(self, name);
    if (idx == -1 && self->var_count >= self->var_capacity) {
        if (self->var_count == MAX_VARS) {
            log_error("maximum number of variables exceeded");
            return;
        }
        int new_capacity = self->var_capacity * 2;
        if(new_capacity > MAX_VARS)
            new_capacity = MAX_VARS;
        Variable **variables = realloc (self->variables, sizeof(Variable) * new_capacity);
        if (!variables) {
            log_error("out of memory");
            return;
        }
    }
    if (idx == -1 && self->var_count < self->var_capacity) {
        char *name_copy = c23_strdup(name);
        if (!name_copy) {
            log_error("out of memory");
            return;
        }
        return_if_null (name);
        char *value_copy = c23_strdup(value);
        if (!value) {
            free (name_copy);
            log_error("out of memory");
            return;
        }

        idx = self->var_count++;
        self->variables[idx].name = name_copy;
        self->variables[idx].value = value_copy;
        self->variables[idx].exported = false;
        self->variables[idx].read_only = false;
    }
    if (idx != -1) {
        if (self->variables[idx].read_only) {
            log_error ("cannot update read-only variable %s", self->variables[idx].name);
            return;
        }
        char *value_copy = c23_strdup(value);
        if (!value_copy) {
            log_error ("out of memory");
            return;
        }
        return_if_null (value_copy);
        free(self->variables[idx].value);
        self->variables[idx].value = value_copy;
        // Should I re-export if this variable is exported
    }
}

void variable_store_export_variable(VariableStore *self, const char *name) {
    return_if_null (self);
    return_if_null (name);
    return_if_eq (strlen(name), 0);
    return_if_gt (strlen(name), MAX_NAME);

    int idx = find_var(self, name);
    if (idx != -1 && !self->variables[idx].exported) {
        self->variables[idx].exported = 1;
        char env[MAX_NAME + MAX_VALUE + 1];
        snprintf(env, sizeof(env), "%s=%s", self->variables[idx].name, self->variables[idx].value);
        char *env_copy = c23_strdup(env);
        if (!env_copy) {
            log_error ("out of memory");
            return;
        }
        /* FIXME: We leak memory intentionally here.  With putenv, the string becomes
         * part of the environment. */
        if (putenv(env_copy) != 0) {
            perror("putenv failed");
        }
    }
}

void variable_store_unset_variable(VariableStore *self, const char *name) {
    return_if_null (self);
    return_if_null (name);
    return_if_eq (strlen(name), 0);
    return_if_gt (strlen(name), MAX_NAME);

    int idx = find_var(self, name);
    if (idx != -1) {
        if (self->variables[idx].exported) {
            char env[MAX_NAME + 1];
            snprintf(env, sizeof(env), "%s=", name);
            char *env_copy = c23_strdup(env);
            if (!env_copy) {
                log_error ("out of memory");
                return;
            }
            if (putenv(env_copy) != 0) {
                perror("putenv failed");
            }
        }
        for (int i = idx; i < self->var_count - 1; i++) {
            self->variables[i] = self->variables[i + 1];
        }
        self->var_count--;
    }
}

const char *variable_store_get_variable(VariableStore *self, const char *name) {
    static const char empty_string[1] = "";
    return_val_if_null (self, empty_string);
    return_val_if_null (name, empty_string);
    return_val_if_eq (strlen(name), 0, empty_string);
    return_val_if_gt (strlen(name), MAX_NAME, empty_string);

    if (strcmp(name, "?") == 0) {
        snprintf(self->status_str, sizeof(self->status_str), "%d", get_last_status());
        return self->status_str;
    }
    int idx = find_var(self, name);
    if (idx != -1) return self->variables[idx].value;
    return "";
}

void variable_store_make_readonly(VariableStore *self, const char *name) {
    return_if_null (self);
    return_if_null (name);
    return_if_eq (strlen(name), 0);
    return_if_gt (strlen(name), MAX_NAME);

    if (strcmp(name, "?") == 0) {
        log_error ("cannot make special variable $? read-only");
        return;
    }
    int idx = find_var(self, name);
    if (idx != -1) {
        log_error("cannot make non-existant variable %s read-only", name);
        return;
    }
    if (!self->variables[idx].read_only) {
        log_debug("setting variable %s read-only", self->variables[idx].name);
        self->variables[idx].read_only = true;
    }
}

void dump_variables(VariableStore *self) {
    for (int i = 0; i < self->var_count; i++) {
        printf("%s=%s", self->variables[i].name, self->variables[i].value);
        if (self->variables[i].exported)
            printf(" exported");
        if (self->variables[i].read_only)
            printf(" read-only");
        printf("\n");
    }
}

#if 0

//////////////////////
void set_variable(Environment *env, const char *assignment) {
    char *expanded = expand_assignment(assignment, env, NULL, NULL);
    char *name = strdup(expanded);
    char *value = strchr(name, '=');
    if (value) {
        *value = '\0';
        value++;
    } else {
        value = "";
    }
    for (int i = 0; i < env->var_count; i++) {
        if (strncmp(env->variables[i].name, name, strlen(name)) == 0 && env->variables[i].name[strlen(name)] == '=') {
            free(env->variables[i].name);
            env->variables[i].name = strdup(expanded);
            free(name);
            free(expanded);
            return;
        }
    }
    if (env->var_count >= env->var_capacity) {
        env->var_capacity *= 2;
        env->variables = realloc(env->variables, env->var_capacity * sizeof(char *));
    }
    env->variables[env->var_count].name = strdup(expanded);
    env->var_count++;
    free(name);
    free(expanded);
}

const char *get_variable(Environment *env, const char *name) {
    for (int i = 0; i < env->var_count; i++) {
        if (strncmp(env->variables[i].name, name, strlen(name)) == 0 && env->variables[i].name[strlen(name)] == '=') {
            return env->variables[i].name + strlen(name) + 1;
        }
    }
    return NULL;
}

void export_variable(Environment *env, const char *name) {
    for (int i = 0; i < env->var_count; i++) {
        if (strcmp(env->variables[i].name, name) == 0) {
            env->variables[i].exported = 1;
            return;
        }
    }
    // If not found, add it with an empty value
    if (env->var_count >= env->var_capacity) {
        env->var_capacity *= 2;
        env->variables = realloc(env->variables, env->var_capacity * sizeof(Variable));
    }
    env->variables[env->var_count].name = strdup(name);
    env->variables[env->var_count].value = strdup("");
    env->variables[env->var_count].exported = 1;
    env->var_count++;
}

void unset_variable(Environment *env, const char *name) {
    for (int i = 0; i < env->var_count; i++) {
        if (strcmp(env->variables[i].name, name) == 0) {
            free(env->variables[i].name);
            free(env->variables[i].value);
            for (int j = i; j < env->var_count - 1; j++) {
                env->variables[j] = env->variables[j + 1];
            }
            env->var_count--;
            return;
        }
    }
}

void show_variables(Environment *env) {
    if (env->var_count == 0) {
        printf("No variables set.\n");
        return;
    }
    for (int i = 0; i < env->var_count; i++) {
        printf("%s%s=%s\n", env->variables[i].exported ? "export " : "",
               env->variables[i].name, env->variables[i].value);
    }
}
#endif
