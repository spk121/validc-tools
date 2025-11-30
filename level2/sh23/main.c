#include <stdio.h>
#include <stdlib.h>
#include "tokenizer.h" // Assume this defines Tokenizer and its functions
#include "token.h"

int main() {
    Tokenizer *tokenizer = tokenizer_create(); // Initialize tokenizer
    char line[1024];                           // Buffer for input line

    while (1) {
        printf("> ");                          // Prompt
        if (!fgets(line, sizeof(line), stdin)) {
            break;                             // Exit on EOF (e.g., Ctrl+D)
        }

        // Remove trailing newline from fgets
        line[strcspn(line, "\n")] = 0;

        // Process the line
        if (tokenizer_process_line(tokenizer, line) != 0) {
            fprintf(stderr, "Error processing line\n");
            break;
        }

        // Check if tokenization is complete
        if (tokenizer_is_complete(tokenizer)) {
            tokenizer_finalize(tokenizer);
            // Print debug output of tokens
            for (size_t i = 0; i < tokenizer_token_count(tokenizer); i++) {
                Token *token = tokenizer_get_token(tokenizer, i);
                String *token_str = token_to_string(token); // Assume this converts token to string
                printf("Token %zu: %s\n", i, string_data(token_str));
                string_destroy(token_str);
            }
            // Reset tokenizer for next input
            tokenizer_clear(tokenizer);
        }
    }

    tokenizer_destroy(tokenizer); // Clean up
    return 0;
}
