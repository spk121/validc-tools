#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

// Operator characters (POSIX shell operators)
static const char *operators[] = {
    "&&", "||", ";", "&", "|", "(", ")", "<", ">", ">>", "<<", NULL
};

// Check if a string is an operator or part of one
static int is_operator_start(char c) {
    return (c == '&' || c == '|' || c == ';' || c == '(' || c == ')' || c == '<' || c == '>');
}

static int is_operator(const char *str) {
    for (int i = 0; operators[i]; i++) {
        if (strncmp(str, operators[i], strlen(operators[i])) == 0) return 1;
    }
    return 0;
}

// Forward declaration for recursive expansion handling
static const char *parse_expansion(const char *input, char *token, int *pos, int max_len, int in_double_quote);

void tokenize(const char *input, char **tokens, int *token_count) {
    *token_count = 0;
    const char *p = input;
    char current_token[MAX_TOKEN_LEN];
    int pos = 0;
    int in_word = 0;

    while (*p) {
        // Rule 1: End of input
        if (*p == '\0') {
            if (in_word) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                tokens[*token_count][MAX_TOKEN_LEN - 1] = '\0';
                (*token_count)++;
            }
            break;
        }

        // Rule 8: Comment (only if not in a word)
        if (*p == '#' && !in_word) {
            while (*p && *p != '\n') p++;
            continue;
        }

        // Rule 3: Quoting
        if (*p == '\\' || *p == '\'' || *p == '"') {
            if (!in_word) in_word = 1;
            if (pos >= MAX_TOKEN_LEN - 1) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
            }

            // Backslash
            if (*p == '\\') {
                current_token[pos++] = *p++;
                if (*p && *p != '\n') { // Exclude newline
                    current_token[pos++] = *p++;
                } else if (*p == '\n') {
                    p++; // Skip newline, backslash escapes it
                }
                continue;
            }

            // Single quotes
            if (*p == '\'') {
                current_token[pos++] = *p++;
                while (*p && *p != '\'') {
                    current_token[pos++] = *p++;
                }
                if (*p == '\'') {
                    current_token[pos++] = *p++;
                } else {
                    fprintf(stderr, "Error: Unmatched single quote\n");
                    return; // Undefined result, stop for now
                }
                continue;
            }

            // Double quotes
            if (*p == '"') {
                current_token[pos++] = *p++;
                while (*p && *p != '"') {
                    if (*p == '\\') {
                        current_token[pos++] = *p++;
                        if (*p && strchr("$`\"\\", *p)) { // Special escapes in double quotes
                            current_token[pos++] = *p++;
                        } else if (*p == '\n') {
                            p++; // Skip newline
                        } else if (*p) {
                            current_token[pos++] = *p++; // Literal backslash + char
                        }
                    } else if (*p == '$' || *p == '`') {
                        p = parse_expansion(p, current_token, &pos, MAX_TOKEN_LEN, 1);
                    } else {
                        current_token[pos++] = *p++;
                    }
                }
                if (*p == '"') {
                    current_token[pos++] = *p++;
                } else {
                    fprintf(stderr, "Error: Unmatched double quote\n");
                    return; // Undefined result
                }
                continue;
            }
        }

        // Rule 4: Expansions (outside quotes)
        if ((*p == '$' || *p == '`') && !in_word) {
            if (!in_word) in_word = 1;
            p = parse_expansion(p, current_token, &pos, MAX_TOKEN_LEN, 0);
            continue;
        }

        // Rule 6: Blank
        if (isspace(*p)) {
            if (in_word) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                tokens[*token_count][MAX_TOKEN_LEN - 1] = '\0';
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            p++;
            continue;
        }

        // Rule 5 & 2: Operator
        if (is_operator_start(*p)) {
            if (in_word) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            current_token[pos++] = *p++;
            if (*p && is_operator_start(*p)) { // Multi-char operator
                current_token[pos++] = *p++;
            }
            current_token[pos] = '\0';
            if (is_operator(current_token)) {
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
            } else {
                pos = 0; // Reset if not valid
            }
            continue;
        }

        // Rule 7 & 9: Word
        if (!in_word) in_word = 1;
        if (pos < MAX_TOKEN_LEN - 1) {
            current_token[pos++] = *p++;
        } else {
            current_token[pos] = '\0';
            strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
            (*token_count)++;
            pos = 0;
            in_word = 0;
        }
    }

    // Delimit final token
    if (in_word) {
        current_token[pos] = '\0';
        strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
        (*token_count)++;
    }
}

static const char *parse_expansion(const char *input, char *token, int *pos, int max_len, int in_double_quote) {
    const char *p = input;

    if (*p == '$') {
        token[(*pos)++] = *p++;
        if (*p == '{') { // ${NAME}
            token[(*pos)++] = *p++;
            int brace_count = 1;
            while (*p && brace_count > 0) {
                if (*p == '{') brace_count++;
                else if (*p == '}') brace_count--;
                if (in_double_quote && *p == '\\' && p[1] && strchr("\"'", p[1])) {
                    token[(*pos)++] = *p++;
                    token[(*pos)++] = *p++;
                } else {
                    token[(*pos)++] = *p++;
                }
            }
            if (brace_count != 0) {
                fprintf(stderr, "Error: Unmatched brace in ${}\n");
            }
        } else if (*p == '(') { // $(cmd)
            token[(*pos)++] = *p++;
            int paren_count = 1;
            while (*p && paren_count > 0) {
                if (*p == '(') paren_count++;
                else if (*p == ')') paren_count--;
                if (in_double_quote && *p == '\\' && p[1] && strchr("\"'", p[1])) {
                    token[(*pos)++] = *p++;
                    token[(*pos)++] = *p++;
                } else if (*p == '$' || *p == '`') {
                    p = parse_expansion(p, token, pos, max_len, in_double_quote);
                } else {
                    token[(*pos)++] = *p++;
                }
            }
            if (paren_count != 0) {
                fprintf(stderr, "Error: Unmatched parenthesis in $()\n");
            }
        } else { // $NAME
            while (*p && (isalnum(*p) || *p == '_')) token[(*pos)++] = *p++;
        }
    } else if (*p == '`') { // `cmd`
        token[(*pos)++] = *p++;
        while (*p && *p != '`') {
            if (*p == '\\' && p[1]) {
                token[(*pos)++] = *p++;
                token[(*pos)++] = *p++;
            } else if (*p == '$' || *p == '`') {
                p = parse_expansion(p, token, pos, max_len, in_double_quote);
            } else {
                token[(*pos)++] = *p++;
            }
        }
        if (*p) token[(*pos)++] = *p++;
        else {
            fprintf(stderr, "Error: Unmatched backquote\n");
        }
    }
    return p;
}

void parse_command(const char *input) {
    (void)input; // Stub
}

void parse_control_structure(const char *input) {
    (void)input; // Stub
}
