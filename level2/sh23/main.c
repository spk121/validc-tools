#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "execute.h"
#include "variables.h"

#define MAX_INPUT 1024

int main() {
    char input[MAX_INPUT];

    while (1) {
        printf("> ");
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) break;
            perror("fgets failed");
            continue;
        }
        input[strcspn(input, "\n")] = 0;

        char *start = input;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '#' || *start == '\0') continue;

        char *eq = strchr(input, '=');
        if (eq && eq > input && !strchr(input, ' ') && strncmp(input, "export", 6) != 0) {
            *eq = '\0';
            char *name = input;
            char *value = eq + 1;
            set_variable(name, value);
        } else if (input[0] != '\0') {
            if (execute_command(input)) {
                break;
            }
        }
    }
    return 0;
}
