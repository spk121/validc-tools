#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "execute.h"
#include "builtins.h"
#include "variables.h"
#include "logging.h"

#define MAX_CMD 1024

static int last_status = 0; // Store last command status

void expand_command(VariableStore *var_store, const char *cmd, char *out) {
    return_if_null (var_store);
    return_if_null (cmd);
    return_if_null (out);

    char *out_ptr = out;
    out[0] = '\0';

    while (*cmd) {
        if (*cmd == '#') break;
        if (*cmd == '$') {
            cmd++;
            char var_name[MAX_CMD];
            int i = 0;
            if (*cmd == '{') {
                cmd++;
                while (*cmd && *cmd != '}' && i < MAX_CMD - 1) {
                    var_name[i++] = *cmd++;
                }
                if (*cmd == '}') cmd++;
            } else {
                while (*cmd && (isalnum(*cmd) || *cmd == '_') && i < MAX_CMD - 1) {
                    var_name[i++] = *cmd++;
                }
            }
            var_name[i] = '\0';
            const char *value = variable_store_get_variable(var_store, var_name);
            strncat(out_ptr, value, MAX_CMD - (out_ptr - out) - 1);
            out_ptr += strlen(value);
        } else {
            if (out_ptr - out < MAX_CMD - 1) *out_ptr++ = *cmd++;
            else break;
        }
    }
    *out_ptr = '\0';
}

void execute_script(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("fopen failed");
        last_status = -1; // Failure to open file
        return;
    }
    unsigned char bom_check[3];
    size_t bytes_read = fread(bom_check, 1, 3, file);
    int has_bom = (bytes_read == 3 && bom_check[0] == 0xEF && bom_check[1] == 0xBB && bom_check[2] == 0xBF);
    if (has_bom) fseek(file, 3, SEEK_SET); else rewind(file);

    char line[MAX_CMD];
    if (fgets(line, sizeof(line), file) && strncmp(line, "#!", 2) == 0) {
        line[strcspn(line, "\n")] = 0;
        char *interpreter = line + 2;
        while (*interpreter == ' ' || *interpreter == '\t') interpreter++;
        char *end = interpreter + strlen(interpreter) - 1;
        while (end > interpreter && (*end == ' ' || *end == '\t' || *end == '\r')) *end-- = 0;

        char cmd[MAX_CMD];
        snprintf(cmd, sizeof(cmd), "%s %s", interpreter, filename);
        int status = system(cmd);
        if (status == -1) {
            perror("system failed");
            last_status = -1;
        } else if (status == 0) {
            last_status = 0;
        } else {
            last_status = status & 0xFF; // Mask with 0xFF to mimic WEXITSTATUS
        }
    } else {
        int status = system(filename);
        if (status == -1) {
            perror("system failed");
            last_status = -1;
        } else if (status == 0) {
            last_status = 0;
        } else {
            last_status = status & 0xFF;
        }
    }
    fclose(file);
}

int execute_command(VariableStore *var_store, const char *cmd) {
    return_val_if_null (var_store, 1);

    if (cmd[0] == '\0') {
        last_status = 0;
        return 0;
    }
    char expanded[MAX_CMD];
    expand_command(var_store, cmd, expanded);

    if (expanded[0] == '\0') {
        last_status = 0;
        return 0;
    }

    if (strncmp(expanded, "exit", 4) == 0 && (expanded[4] == '\0' || expanded[4] == ' ')) {
        int status = 0;
        if (expanded[4] == ' ') status = atoi(expanded + 5);
        builtin_exit(status);
        last_status = 0; // Not used since we exit
        return 1;
    }

    if (strncmp(expanded, "export", 6) == 0 && (expanded[6] == ' ' || expanded[6] == '\0')) {
        char *arg = expanded + 6;
        while (*arg == ' ') arg++;
        char *eq = strchr(arg, '=');
        if (eq) {
            *eq = '\0';
            char *name = arg;
            char *value = eq + 1;
            variable_store_set_variable(var_store, name, value);
            builtin_export(name);
        } else if (*arg) {
            builtin_export(arg);
        }
        last_status = 0; // Success
        return 0;
    }

    if (strncmp(expanded, "unset", 5) == 0 && (expanded[5] == ' ' || expanded[5] == '\0')) {
        char *arg = expanded + 5;
        while (*arg == ' ') arg++;
        if (*arg) {
            builtin_unset(arg);
            last_status = 0; // Success
        } else {
            last_status = 1; // No argument, failure
        }
        return 0;
    }

    if (strncmp(expanded, "echo", 4) == 0 && (expanded[4] == '\0' || expanded[4] == ' ')) {
        char *args = expanded + 4;
        while (*args == ' ') args++;
        builtin_echo(args);
        last_status = 0; // Success
        return 0;
    }

    if (strcmp(expanded, "%showvars") == 0) {
        builtin_showvars();
        last_status = 0; // Success
        return 0;
    }

    if (strncmp(expanded, "./", 2) == 0 || expanded[0] == '/') {
        execute_script(expanded);
    } else {
        int status = system(expanded);
        if (status == -1) {
            perror("system failed");
            last_status = -1;
        } else if (status == 0) {
            last_status = 0;
        } else {
            last_status = status & 0xFF; // Mask with 0xFF
        }
    }
    return 0;
}

int get_last_status(void) {
    return last_status;
}
