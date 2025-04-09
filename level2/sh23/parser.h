#ifndef PARSER_H
#define PARSER_H

#define MAX_TOKENS 128
#define MAX_TOKEN_LEN 1024

// Tokenize input into an array of strings
void tokenize(const char *input, char **tokens, int *token_count);

void parse_command(const char *input);
void parse_control_structure(const char *input);

#endif
