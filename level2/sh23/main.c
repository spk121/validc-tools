#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "builtins.h"

#define MAX_INPUT 1024

int main() {
    char input[MAX_INPUT];
    char *tokens[MAX_TOKENS];
    int token_count;

    for (int i = 0; i < MAX_TOKENS; i++) {
        tokens[i] = malloc(MAX_TOKEN_LEN);
        if (!tokens[i]) {
            perror("malloc failed");
            exit(1);
        }
    }

    // Predefine some aliases
    builtin_alias("ll", "ls -l");
    builtin_alias("dir", "ls dir ");

    while (1) {
        printf("> ");
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) break;
            perror("fgets failed");
            continue;
        }
        input[strcspn(input, "\n")] = 0;

        tokenize(input, tokens, &token_count);
        printf("Tokens (%d):\n", token_count);
        for (int i = 0; i < token_count; i++) {
            printf("  [%d]: '%s'\n", i, tokens[i]);
        }
    }

    for (int i = 0; i < MAX_TOKENS; i++) {
        free(tokens[i]);
    }
    return 0;
}
