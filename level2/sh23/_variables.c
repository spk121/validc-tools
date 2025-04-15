#include "variables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Create a new Variable
static Variable *variable_create(const char *name, const char *value) {
    Variable *var = malloc(sizeof(Variable));
    if (!var) return NULL;

    var->name = string_create(16);
    var->value = string_create(16);
    if (!var->name || !var->value) {
        string_destroy(var->name);
        string_destroy(var->value);
        free(var);
        return NULL;
    }

    string_append_zstring(var->name, name);
    string_append_zstring(var->value, value);
    var->exported = false;
    var->read_only = false;
    return var;
}

// Free a Variable
static void variable_free(Variable *var) {
    if (!var) return;
    string_destroy(var->name);
    string_destroy(var->value);
    free(var);
}

// Free a String
static void string_free(void *s) {
    string_destroy((String *)s);
}

// Find variable by name, return index or -1 if not found
static int find_variable_index(PtrArray *vars, const char *name) {
    for (int i = 0; i < vars->len; i++) {
        Variable *var = vars->data[i];
        if (strcmp(string_data(var->name), name) == 0) {
            return i;
        }
    }
    return -1;
}

// Check if name is a special variable
static bool is_special_variable(const char *name) {
    return strcmp(name, "?") == 0 ||
           strcmp(name, "#") == 0 ||
           strcmp(name, "$") == 0 ||
           strcmp(name, "!") == 0 ||
           strcmp(name, "0") == 0 ||
           strcmp(name, "@") == 0 ||
           strcmp(name, "*") == 0 ||
           strcmp(name, "-") == 0;
}

// Create VariableStore
VariableStore *variable_store_create(const char *shell_name) {
    VariableStore *store = malloc(sizeof(VariableStore));
    if (!store) return NULL;

    store->variables = ptr_array_create();
    store->positional_params = ptr_array_create();
    if (!store->variables || !store->positional_params) {
        ptr_array_destroy(store->variables, (FreeFunc)variable_free);
        ptr_array_destroy(store->positional_params, string_free);
        free(store);
        return NULL;
    }

    snprintf(store->status_str, sizeof(store->status_str), "0");
    store->pid = getpid();
    store->shell_name = shell_name ? strdup(shell_name) : strdup("myshell");
    if (!store->shell_name) {
        ptr_array_destroy(store->variables, (FreeFunc)variable_free);
        ptr_array_destroy(store->positional_params, string_free);
        free(store);
        return NULL;
    }
    store->last_bg_pid = 0;
    strcpy(store->options, "i"); // Interactive by default
    return store;
}

// Destroy VariableStore
void variable_store_destroy(VariableStore *self) {
    if (!self) return;
    ptr_array_destroy(self->variables, (FreeFunc)variable_free);
    ptr_array_destroy(self->positional_params, string_free);
    free(self->shell_name);
    free(self);
}

// Set or update variable
void variable_store_set_variable(VariableStore *self, const char *name, const char *value) {
    if (!self || !name || !value) return;

    if (is_special_variable(name)) {
        fprintf(stderr, "Cannot set special variable %s\n", name);
        return;
    }

    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = self->variables->data[idx];
        if (var->read_only) {
            fprintf(stderr, "Variable %s is read-only\n", name);
            return;
        }
        string_clear(var->value);
        string_append_zstring(var->value, value);
    } else {
        Variable *var = variable_create(name, value);
        if (!var) {
            fprintf(stderr, "Memory allocation failed for variable %s\n", name);
            return;
        }
        ptr_array_append(self->variables, var);
    }
}

// Export variable
void variable_store_export_variable(VariableStore *self, const char *name) {
    if (!self || !name) return;

    if (is_special_variable(name)) {
        fprintf(stderr, "Cannot export special variable %s\n", name);
        return;
    }

    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = self->variables->data[idx];
        var->exported = true;
    } else {
        Variable *var = variable_create(name, "");
        if (!var) {
            fprintf(stderr, "Memory allocation failed for export %s\n", name);
            return;
        }
        var->exported = true;
        ptr_array_append(self->variables, var);
    }
}

// Unset variable
void variable_store_unset_variable(VariableStore *self, const char *name) {
    if (!self || !name) return;

    if (is_special_variable(name)) {
        fprintf(stderr, "Cannot unset special variable %s\n", name);
        return;
    }

    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = self->variables->data[idx];
        if (var->read_only) {
            fprintf(stderr, "Variable %s is read-only\n", name);
            return;
        }
        ptr_array_remove(self->variables, idx, (FreeFunc)variable_free);
    }
}

// Get variable value
const char *variable_store_get_variable(VariableStore *self, const char *name) {
    if (!self || !name) return NULL;

    // Special variables
    if (strcmp(name, "?") == 0) {
        return self->status_str;
    }
    if (strcmp(name, "#") == 0) {
        static char num[16];
        snprintf(num, sizeof(num), "%d", self->positional_params->len);
        return num;
    }
    if (strcmp(name, "$") == 0) {
        static char pid[16];
        snprintf(pid, sizeof(pid), "%d", self->pid);
        return pid;
    }
    if (strcmp(name, "!") == 0) {
        if (self->last_bg_pid == 0) return "";
        static char pid[16];
        snprintf(pid, sizeof(pid), "%d", self->last_bg_pid);
        return pid;
    }
    if (strcmp(name, "0") == 0) {
        return self->shell_name;
    }
    if (strcmp(name, "@") == 0 || strcmp(name, "*") == 0) {
        if (self->positional_params->len == 0) return "";
        String *result = string_create(256);
        if (!result) return "";
        for (int i = 0; i < self->positional_params->len; i++) {
            if (i > 0) string_append_char(result, ' ');
            string_append_zstring(result, string_data(self->positional_params->data[i]));
        }
        const char *value = string_data(result);
        string_destroy(result);
        return value;
    }
    if (strcmp(name, "-") == 0) {
        return self->options;
    }

    // Positional parameters ($1, $2, ...)
    if (isdigit(name[0]) && name[0] != '0') {
        int idx = atoi(name) - 1;
        if (idx >= 0 && idx < self->positional_params->len) {
            return string_data(self->positional_params->data[idx]);
        }
        return "";
    }

    // Regular variables
    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = self->variables->data[idx];
        return string_data(var->value);
    }
    return "";
}

// Make variable read-only
void variable_store_make_readonly(VariableStore *self, const char *name) {
    if (!self || !name) return;

    if (is_special_variable(name)) {
        fprintf(stderr, "Cannot make special variable %s read-only\n", name);
        return;
    }

    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = self->variables->data[idx];
        var->read_only = true;
    } else {
        Variable *var = variable_create(name, "");
        if (!var) {
            fprintf(stderr, "Memory allocation failed for readonly %s\n", name);
            return;
        }
        var->read_only = true;
        ptr_array_append(self->variables, var);
    }
}

// Dump all variables (for debugging)
void variable_store_dump_variables(VariableStore *self) {
    if (!self) {
        printf("VariableStore is NULL\n");
        return;
    }

    printf("Special Variables:\n");
    printf("  ?=%s\n", variable_store_get_variable(self, "?"));
    printf("  #=%s\n", variable_store_get_variable(self, "#"));
    printf("  $=%s\n", variable_store_get_variable(self, "$"));
    printf("  !=%s\n", variable_store_get_variable(self, "!"));
    printf("  0=%s\n", variable_store_get_variable(self, "0"));
    printf("  @=%s\n", variable_store_get_variable(self, "@"));
    printf("  *=%s\n", variable_store_get_variable(self, "*"));
    printf("  -=%s\n", variable_store_get_variable(self, "-"));
    for (int i = 0; i < self->positional_params->len; i++) {
        printf("  %d=%s\n", i + 1, string_data(self->positional_params->data[i]));
    }

    printf("Regular Variables:\n");
    for (int i = 0; i < self->variables->len; i++) {
        Variable *var = self->variables->data[i];
        printf("  %s=%s [exported=%d, read_only=%d]\n",
               string_data(var->name),
               string_data(var->value),
               var->exported,
               var->read_only);
    }
}

// Set exit status
void variable_store_set_status(VariableStore *self, int status) {
    if (!self) return;
    snprintf(self->status_str, sizeof(self->status_str), "%d", status);
}

// Set positional parameters
void variable_store_set_positional_params(VariableStore *self, int argc, char **argv) {
    if (!self) return;
    ptr_array_clear(self->positional_params, string_free);
    for (int i = 0; i < argc; i++) {
        String *param = string_create(16);
        if (!param) continue;
        string_append_zstring(param, argv[i]);
        ptr_array_append(self->positional_params, param);
    }
}

// Set background PID
void variable_store_set_background_pid(VariableStore *self, pid_t pid) {
    if (!self) return;
    self->last_bg_pid = pid;
}

// Set shell options
void variable_store_set_options(VariableStore *self, const char *opts) {
    if (!self || !opts) return;
    strncpy(self->options, opts, sizeof(self->options) - 1);
    self->options[sizeof(self->options) - 1] = '\0';
}
