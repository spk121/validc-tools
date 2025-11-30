#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_COMMAND 1024

void print_help(void) {
    printf("Usage: ifc value1 operator value2 command [args ...]\n");
    printf("Evaluate a condition and execute a command with arguments if true.\n");
    printf("Operators: ==, !=, <, >, <=, >=\n");
    printf("Options:\n");
    printf("  -h, --help  Display this help message\n");
    printf("Returns 0 if command succeeds or condition is false, 1 on error or command failure.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: Too few arguments\n");
        print_help();
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }

    if (argc < 5) {
        fprintf(stderr, "Error: Insufficient arguments (need value1, operator, value2, command)\n");
        print_help();
        return 1;
    }

    const char *value1_str = argv[1];
    const char *operator = argv[2];
    const char *value2_str = argv[3];

    // Build command string from argv[4] onward
    char command[MAX_COMMAND] = {0};
    size_t cmd_len = 0;
    for (int i = 4; i < argc; i++) {
        size_t arg_len = strlen(argv[i]);
        if (cmd_len + arg_len + (i > 4 ? 1 : 0) >= MAX_COMMAND - 1) {
            fprintf(stderr, "Error: Command too long (max %d characters)\n", MAX_COMMAND - 1);
            return 1;
        }
        if (i > 4) {
            command[cmd_len++] = ' '; // Add space between arguments
        }
        strcpy(&command[cmd_len], argv[i]);
        cmd_len += arg_len;
    }

    // Convert values to integers
    char *endptr;
    long value1 = strtol(value1_str, &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "Error: Invalid number '%s'\n", value1_str);
        return 1;
    }
    long value2 = strtol(value2_str, &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "Error: Invalid number '%s'\n", value2_str);
        return 1;
    }

    // Evaluate condition
    bool condition = false;
    if (strcmp(operator, "==") == 0) {
        condition = (value1 == value2);
    } else if (strcmp(operator, "!=") == 0) {
        condition = (value1 != value2);
    } else if (strcmp(operator, "<") == 0) {
        condition = (value1 < value2);
    } else if (strcmp(operator, ">") == 0) {
        condition = (value1 > value2);
    } else if (strcmp(operator, "<=") == 0) {
        condition = (value1 <= value2);
    } else if (strcmp(operator, ">=") == 0) {
        condition = (value1 >= value2);
    } else {
        fprintf(stderr, "Error: Unknown operator '%s'\n", operator);
        print_help();
        return 1;
    }

    // Execute command if condition is true
    if (condition) {
        int result = system(command);
        return (result == 0) ? 0 : 1;
    }

    return 0; // Condition false, no command executed
}