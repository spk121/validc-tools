#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_LINE 1024
#define MAX_VARS 100
#define MAX_VAR_NAME 32
#define MAX_VAR_VALUE 256

typedef struct {
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
} Variable;

void print_help(void) {
    printf("Usage: batch [options] [filename]\n");
    printf("Options:\n");
    printf("  -h, --help         Display this help message\n");
    printf("  -v, --verbose      Print commands and return values\n");
    printf("  -i, --ignore-errors  Continue execution even if a command fails\n");
    printf("  -n, --dry-run      Print commands without executing them\n");
    printf("If no filename is provided, reads from stdin\n");
}

bool is_valid_var_char(char c) {
    return (c >= 32 && c <= 126 && c != '(' && c != ')' && c != '$' && c != '=');
}

void strip_cr(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\r') {
        str[len - 1] = 0;
    }
}

int main(int argc, char *argv[]) {
    FILE *input = stdin;
    Variable vars[MAX_VARS] = {0};
    size_t var_count = 0;
    char line[MAX_LINE];
    char full_command[MAX_LINE] = {0};
    bool continuing = false;
    bool verbose = false;
    bool ignore_errors = false;
    bool dry_run = false;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--ignore-errors") == 0) {
            ignore_errors = true;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (filename == NULL) {
            filename = argv[i];
        } else {
            fprintf(stderr, "Error: Too many arguments\n");
            print_help();
            return EXIT_FAILURE;
        }
    }

    if (filename != NULL) {
        input = fopen(filename, "r");
        if (!input) {
            fprintf(stderr, "Error: Could not open file '%s'\n", filename);
            return EXIT_FAILURE;
        }
    }

    while (fgets(line, sizeof(line), input) != NULL) {
        size_t len = strlen(line);
        bool has_newline = (len > 0 && line[len - 1] == '\n');
        line[strcspn(line, "\n")] = 0;
        strip_cr(line);
        
        if (!has_newline && !feof(input)) {
            fprintf(stderr, "Error: Incomplete line detected (exceeds %d characters or missing newline)\n", 
                    MAX_LINE - 1);
            if (input != stdin) fclose(input);
            return EXIT_FAILURE;
        }

        if (strlen(line) == 0 && !continuing) {
            continue;
        }

        if (!continuing && line[0] == ';') {
            continue;
        }

        len = strlen(line);
        bool has_continuation = (len > 0 && line[len - 1] == '\\');
        
        if (has_continuation) {
            line[len - 1] = 0;
        }

        if (continuing) {
            strncat(full_command, line, MAX_LINE - strlen(full_command) - 1);
        } else {
            strncpy(full_command, line, MAX_LINE - 1);
        }
        full_command[MAX_LINE - 1] = 0;

        if (!has_continuation) {
            if (strncmp(full_command, "var", 3) == 0) {
                char *ptr = full_command + 3;
                
                while (isspace((unsigned char)*ptr)) {
                    ptr++;
                }
                
                char var_name[MAX_VAR_NAME + 1] = {0};
                size_t name_pos = 0;
                while (is_valid_var_char(*ptr) && name_pos < MAX_VAR_NAME) {
                    var_name[name_pos++] = *ptr++;
                }
                
                if (name_pos == 0) {
                    fprintf(stderr, "Warning: Invalid or missing variable name\n");
                    goto next_line;
                }
                
                if (strlen(var_name) > MAX_VAR_NAME - 1) {
                    fprintf(stderr, "Warning: Variable name '%s' exceeds maximum length of %d\n", 
                           var_name, MAX_VAR_NAME - 1);
                    goto next_line;
                }
                
                for (size_t i = 0; i < name_pos; i++) {
                    if (!is_valid_var_char(var_name[i])) {
                        fprintf(stderr, "Warning: Invalid character in variable name '%s'\n", var_name);
                        goto next_line;
                    }
                }
                
                while (isspace((unsigned char)*ptr)) {
                    ptr++;
                }
                
                if (*ptr != '=') {
                    fprintf(stderr, "Warning: Missing equals sign in var command\n");
                    goto next_line;
                }
                ptr++;
                
                char var_value[MAX_VAR_VALUE + 1];
                strncpy(var_value, ptr, MAX_VAR_VALUE);
                var_value[MAX_VAR_VALUE] = 0;
                
                if (strlen(ptr) > MAX_VAR_VALUE - 1) {
                    fprintf(stderr, "Warning: Variable value for '%s' exceeds maximum length of %d, truncated\n", 
                           var_name, MAX_VAR_VALUE - 1);
                    var_value[MAX_VAR_VALUE - 1] = 0;
                }

                size_t i;
                for (i = 0; i < var_count; i++) {
                    if (strcmp(vars[i].name, var_name) == 0) {
                        strncpy(vars[i].value, var_value, MAX_VAR_VALUE - 1);
                        vars[i].value[MAX_VAR_VALUE - 1] = 0;
                        break;
                    }
                }
                if (i == var_count) {
                    if (var_count >= MAX_VARS) {
                        fprintf(stderr, "Warning: Maximum number of variables (%d) exceeded\n", MAX_VARS);
                    } else {
                        strncpy(vars[var_count].name, var_name, MAX_VAR_NAME - 1);
                        vars[var_count].name[MAX_VAR_NAME - 1] = 0;
                        strncpy(vars[var_count].value, var_value, MAX_VAR_VALUE - 1);
                        vars[var_count].value[MAX_VAR_VALUE - 1] = 0;
                        var_count++;
                    }
                }
            } else {
                char command[MAX_LINE] = {0};
                size_t line_pos = 0;
                size_t cmd_pos = 0;
                
                while (full_command[line_pos] != 0 && cmd_pos < MAX_LINE - 1) {
                    if (full_command[line_pos] == '{' && full_command[line_pos + 1] == '{') {
                        char var_name[MAX_VAR_NAME] = {0};
                        size_t name_pos = 0;
                        line_pos += 2;
                        
                        while (full_command[line_pos] != '}' && 
                               full_command[line_pos + 1] != '}' && 
                               full_command[line_pos] != 0 && 
                               name_pos < MAX_VAR_NAME - 1) {
                            var_name[name_pos++] = full_command[line_pos++];
                        }
                        
                        if (full_command[line_pos] == '}' && full_command[line_pos + 1] == '}') {
                            line_pos += 2;
                            for (size_t i = 0; i < var_count; i++) {
                                if (strcmp(vars[i].name, var_name) == 0) {
                                    size_t value_len = strlen(vars[i].value);
                                    if (cmd_pos + value_len < MAX_LINE - 1) {
                                        strcpy(&command[cmd_pos], vars[i].value);
                                        cmd_pos += value_len;
                                    }
                                    break;
                                }
                            }
                        }
                    } else {
                        command[cmd_pos++] = full_command[line_pos++];
                    }
                }

                if (dry_run) {
                    printf("Would execute: %s\n", command);
                } else {
                    if (verbose) {
                        printf("Executing: %s\n", command);
                    }
                    int result = system(command);
                    if (verbose) {
                        printf("Return value: %d\n", result);
                    }
                    if (result != 0 && !ignore_errors) {
                        fprintf(stderr, "Error: Command '%s' failed with return value %d\n", command, result);
                        if (input != stdin) fclose(input);
                        return EXIT_FAILURE;
                    }
                }
            }
next_line:
            full_command[0] = 0;
            continuing = false;
        } else {
            continuing = true;
        }
    }

    if (input != stdin) {
        fclose(input);
    }
    
    return EXIT_SUCCESS;
}