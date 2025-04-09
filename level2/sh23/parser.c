#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"
#include "builtins.h"

static const char *operators[] = {
    "&&", "||", ";", "&", "|", "(", ")", "<", ">", ">>", "<<", NULL
};

static int is_operator_start(char c) {
    return (c == '&' || c == '|' || c == ';' || c == '(' || c == ')' || c == '<' || c == '>');
}

static int is_operator(const char *str) {
    for (int i = 0; operators[i]; i++) {
        if (strncmp(str, operators[i], strlen(operators[i])) == 0) return 1;
    }
    return 0;
}

static const char *parse_expansion(const char *input, char *token, int *pos, int max_len, int in_double_quote);

void tokenize(const char *input, char **tokens, int *token_count) {
    *token_count = 0;
    const char *p = input;
    char current_token[MAX_TOKEN_LEN];
    int pos = 0;
    int in_word = 0;

    while (*p) {
        if (*p == '\0') {
            if (in_word) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                (*token_count)++;
            }
            break;
        }

        if (*p == '#' && !in_word) {
            while (*p && *p != '\n') p++;
            continue;
        }

        if (*p == '\\' || *p == '\'' || *p == '"') {
            if (!in_word) in_word = 1;
            if (pos >= MAX_TOKEN_LEN - 1) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
            }

            if (*p == '\\') {
                current_token[pos++] = *p++;
                if (*p && *p != '\n') {
                    current_token[pos++] = *p++;
                } else if (*p == '\n') {
                    p++;
                }
                continue;
            }

            if (*p == '\'') {
                current_token[pos++] = *p++;
                while (*p && *p != '\'') {
                    current_token[pos++] = *p++;
                }
                if (*p == '\'') {
                    current_token[pos++] = *p++;
                } else {
                    fprintf(stderr, "Error: Unmatched single quote\n");
                    return;
                }
                continue;
            }

            if (*p == '"') {
                current_token[pos++] = *p++;
                while (*p && *p != '"') {
                    if (*p == '\\') {
                        current_token[pos++] = *p++;
                        if (*p && strchr("$`\"\\", *p)) {
                            current_token[pos++] = *p++;
                        } else if (*p == '\n') {
                            p++;
                        } else if (*p) {
                            current_token[pos++] = *p++;
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
                    return;
                }
                continue;
            }
        }

        if ((*p == '$' || *p == '`') && !in_word) {
            if (!in_word) in_word = 1;
            p = parse_expansion(p, current_token, &pos, MAX_TOKEN_LEN, 0);
            continue;
        }

        if (isspace(*p)) {
            if (in_word) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            p++;
            continue;
        }

        if (is_operator_start(*p)) {
            if (in_word) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            current_token[pos++] = *p++;
            if (*p && is_operator_start(*p)) {
                current_token[pos++] = *p++;
            }
            current_token[pos] = '\0';
            if (is_operator(current_token)) {
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
            } else {
                pos = 0;
            }
            continue;
        }

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

    if (in_word) {
        current_token[pos] = '\0';
        strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
        (*token_count)++;
    }

    // Alias substitution for the first token (command name)
    if (*token_count > 0) {
        char alias_value[MAX_TOKEN_LEN];
        if (substitute_alias(tokens[0], alias_value, MAX_TOKEN_LEN)) {
            // Replace first token with alias value
            strncpy(tokens[0], alias_value, MAX_TOKEN_LEN);
            tokens[0][MAX_TOKEN_LEN - 1] = '\0';

            // Check if alias ends in blank and substitute next word
            int len = strlen(alias_value);
            if (len > 0 && isspace(alias_value[len - 1]) && *token_count > 1) {
                if (substitute_alias(tokens[1], alias_value, MAX_TOKEN_LEN)) {
                    strncpy(tokens[1], alias_value, MAX_TOKEN_LEN);
                    tokens[1][MAX_TOKEN_LEN - 1] = '\0';
                }
            }
        }
    }
}

static const char *parse_expansion(const char *input, char *token, int *pos, int max_len, int in_double_quote) {
    const char *p = input;

    if (*p == '$') {
        token[(*pos)++] = *p++;
        if (*p == '{') {
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
        } else if (*p == '(') {
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
        } else {
            while (*p && (isalnum(*p) || *p == '_')) token[(*pos)++] = *p++;
        }
    } else if (*p == '`') {
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
