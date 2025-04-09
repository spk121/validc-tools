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
static const char *parse_expansion(const char *input, char *token, int *pos, int max_len);

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
                in_word = 0;
            }
            break;
        }

        // Rule 8: Comment
        if (*p == '#' && !in_word) {
            while (*p && *p != '\n') p++; // Skip to newline
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
            if (*p == '\\') {
                current_token[pos++] = *p++;
                if (*p) current_token[pos++] = *p++;
            } else if (*p == '\'') {
                current_token[pos++] = *p++;
                while (*p && *p != '\'') current_token[pos++] = *p++;
                if (*p) current_token[pos++] = *p++;
            } else if (*p == '"') {
                current_token[pos++] = *p++;
                while (*p && *p != '"') {
                    if (*p == '\\' && p[1]) {
                        current_token[pos++] = *p++;
                        current_token[pos++] = *p++;
                    } else {
                        current_token[pos++] = *p++;
                    }
                }
                if (*p) current_token[pos++] = *p++;
            }
            continue;
        }

        // Rule 4: Expansions
        if (*p == '$' || *p == '`') {
            if (!in_word) in_word = 1;
            p = parse_expansion(p, current_token, &pos, MAX_TOKEN_LEN);
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
            if (*p && is_operator_start(*p)) { // Check for multi-char operator
                current_token[pos++] = *p++;
            }
            current_token[pos] = '\0';
            if (is_operator(current_token)) {
                strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
            } else {
                pos = 0; // Reset if not a valid operator
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

    // Delimit final token if any
    if (in_word) {
        current_token[pos] = '\0';
        strncpy(tokens[*token_count], current_token, MAX_TOKEN_LEN);
        (*token_count)++;
    }
}

// Handle parameter expansion, command substitution, and arithmetic expansion
static const char *parse_expansion(const char *input, char *token, int *pos, int max_len) {
    const char *p = input;
    if (*p == '$') {
        token[(*pos)++] = *p++;
        if (*p == '{') { // ${NAME}
            token[(*pos)++] = *p++;
            while (*p && *p != '}') token[(*pos)++] = *p++;
            if (*p) token[(*pos)++] = *p++;
        } else if (*p == '(') { // $(cmd)
            token[(*pos)++] = *p++;
            int paren_count = 1;
            while (*p && paren_count > 0) {
                if (*p == '(') paren_count++;
                else if (*p == ')') paren_count--;
                token[(*pos)++] = *p++;
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
            } else {
                token[(*pos)++] = *p++;
            }
        }
        if (*p) token[(*pos)++] = *p++;
    }
    return p;
}

void parse_command(const char *input) {
    (void)input; // Stub
}

void parse_control_structure(const char *input) {
    (void)input; // Stub
}
