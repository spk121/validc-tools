#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "parser.h"
#include "builtins.h"

ParserState *current_parser_state = NULL;

static const char *operators[] = {
    "&&", "||", ";;", "<<", ">>", "<&", ">&", "<>", "<<-", ">|",
    "<", ">", "|", ";", "&", "(", ")", NULL
};

static const char *reserved_words[] = {
    "if", "then", "else", "elif", "fi", "do", "done", "case", "esac",
    "while", "until", "for", "{", "}", "!", "in", NULL
};


// Helper to append to a dynamic string
static void append_to_string(char **str, int *capacity, int *len, char c) {
    if (*len >= *capacity - 1) {
        *capacity *= 2;
        *str = realloc(*str, *capacity);
    }
    (*str)[(*len)++] = c;
    (*str)[*len] = '\0';
}

static int is_operator_start(char c) {
    return (c == '&' || c == '|' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')');
}

static int is_operator(const char *str) {
    for (int i = 0; operators[i]; i++) {
        if (strcmp(str, operators[i]) == 0) return 1;
    }
    return 0;
}

static TokenType get_operator_type(const char *op) {
    if (strcmp(op, "&&") == 0) return AND_IF;
    if (strcmp(op, "||") == 0) return OR_IF;
    if (strcmp(op, ";;") == 0) return DSEMI;
    if (strcmp(op, "<<") == 0) return DLESS;
    if (strcmp(op, ">>") == 0) return DGREAT;
    if (strcmp(op, "<&") == 0) return LESSAND;
    if (strcmp(op, ">&") == 0) return GREATAND;
    if (strcmp(op, "<>") == 0) return LESSGREAT;
    if (strcmp(op, "<<-") == 0) return DLESSDASH;
    if (strcmp(op, ">|") == 0) return CLOBBER;
    if (strcmp(op, "<") == 0) return LESS;
    if (strcmp(op, ">") == 0) return GREAT;
    if (strcmp(op, "|") == 0) return PIPE;
    if (strcmp(op, ";") == 0) return SEMI;
    if (strcmp(op, "&") == 0) return AMP;
    if (strcmp(op, "(") == 0) return LPAREN;
    if (strcmp(op, ")") == 0) return RPAREN;
    return TOKEN;
}

static TokenType get_reserved_word_type(const char *word) {
    for (int i = 0; reserved_words[i]; i++) {
        if (strcmp(word, reserved_words[i]) == 0) {
            switch (i) {
                case 0: return IF;
                case 1: return THEN;
                case 2: return ELSE;
                case 3: return ELIF;
                case 4: return FI;
                case 5: return DO;
                case 6: return DONE;
                case 7: return CASE;
                case 8: return ESAC;
                case 9: return WHILE;
                case 10: return UNTIL;
                case 11: return FOR;
                case 12: return LBRACE;
                case 13: return RBRACE;
                case 14: return BANG;
                case 15: return IN;
                default: return TOKEN;
            }
        }
    }
    return TOKEN;
}

static int is_valid_name(const char *name) {
    if (!name || !*name || isdigit(*name)) return 0;
    for (const char *p = name; *p; p++) {
        if (!isalnum(*p) && *p != '_') return 0;
    }
    return 1;
}

static int is_all_digits(const char *str) {
    for (const char *p = str; *p; p++) {
        if (!isdigit(*p)) return 0;
    }
    return *str != '\0';
}


static const char *parse_expansion(const char *p, char *current_token, int *pos,
                                  int max_len, int in_quotes, int depth,
                                  char **active_aliases, int active_alias_count) {
    if (*p == '$') {
        p++;
        if (*p == '{') {
            p++;
            char var_name[MAX_TOKEN_LEN] = {0};
            int var_pos = 0;
            int is_advanced = 0;
            char op = '\0';
            char *colon = NULL;

            // Check for # (length or prefix removal)
            if (*p == '#') {
                is_advanced = 1;
                op = '#';
                p++;
                if (*p == '#') {
                    op = 'H'; // ## (longest match)
                    p++;
                }
            }

            // Parse variable name
            while (*p && *p != '}' && *p != ':' && *p != '#' && *p != '%' && var_pos < MAX_TOKEN_LEN - 1) {
                var_name[var_pos++] = *p++;
            }
            var_name[var_pos] = '\0';

            // Check for advanced expansions
            if (*p == ':') {
                colon = (char *)p;
                p++;
                if (*p == '-' || *p == '=' || *p == '?' || *p == '+') {
                    is_advanced = 1;
                    op = *p;
                    p++;
                } else if (isdigit(*p) || *p == '-') {
                    is_advanced = 1;
                    op = 'S'; // Substring
                }
            } else if (*p == '#' || *p == '%') {
                is_advanced = 1;
                op = *p;
                p++;
                if (*p == '#' || *p == '%') {
                    op = (*p == '#') ? 'H' : 'P'; // ## or %%
                    p++;
                }
            }

            // Collect rest until }
            char rest[MAX_TOKEN_LEN] = {0};
            int rest_pos = 0;
            while (*p && *p != '}' && rest_pos < MAX_TOKEN_LEN - 1) {
                rest[rest_pos++] = *p++;
            }
            rest[rest_pos] = '\0';

            if (*p == '}') {
                p++;
            } else {
                fprintf(stderr, "Error: Unmatched '}' in parameter expansion\n");
                return p;
            }

            // Store expansion in token
            if (*pos < max_len - 1) current_token[(*pos)++] = '$';
            if (*pos < max_len - 1) current_token[(*pos)++] = '{';
            if (op == '#') {
                if (*pos < max_len - 1) current_token[(*pos)++] = '#';
            } else if (op == 'H') {
                if (*pos < max_len - 1) current_token[(*pos)++] = '#';
                if (*pos < max_len - 1) current_token[(*pos)++] = '#';
            }
            for (int i = 0; var_name[i] && *pos < max_len - 1; i++) {
                current_token[(*pos)++] = var_name[i];
            }
            if (colon) {
                if (*pos < max_len - 1) current_token[(*pos)++] = ':';
            }
            if (op && op != '#' && op != 'H' && op != '%' && op != 'P') {
                if (*pos < max_len - 1) current_token[(*pos)++] = op;
            } else if (op == '%') {
                if (*pos < max_len - 1) current_token[(*pos)++] = '%';
            } else if (op == 'P') {
                if (*pos < max_len - 1) current_token[(*pos)++] = '%';
                if (*pos < max_len - 1) current_token[(*pos)++] = '%';
            }
            for (int i = 0; rest[i] && *pos < max_len - 1; i++) {
                current_token[(*pos)++] = rest[i];
            }
            if (*pos < max_len - 1) current_token[(*pos)++] = '}';
            return p;
        } else if (*p == '(' && p[1] == '(') {
            // Arithmetic expansion
            p += 2;
            int paren_count = 1;
            char expr[MAX_TOKEN_LEN] = {0};
            int expr_pos = 0;
            while (*p && paren_count > 0 && expr_pos < MAX_TOKEN_LEN - 1) {
                if (*p == '(') paren_count++;
                else if (*p == ')') paren_count--;
                if (paren_count > 0) {
                    expr[expr_pos++] = *p;
                }
                p++;
            }
            expr[expr_pos] = '\0';
            if (paren_count == 0 && *p == ')') {
                p++; // Consume final )
                if (*pos < max_len - 1) current_token[(*pos)++] = '$';
                if (*pos < max_len - 1) current_token[(*pos)++] = '(';
                if (*pos < max_len - 1) current_token[(*pos)++] = '(';
                for (int i = 0; expr[i] && *pos < max_len - 1; i++) {
                    current_token[(*pos)++] = expr[i];
                }
                if (*pos < max_len - 1) current_token[(*pos)++] = ')';
                if (*pos < max_len - 1) current_token[(*pos)++] = ')';
            } else {
                fprintf(stderr, "Error: Unmatched '))' in arithmetic expansion\n");
            }
            return p;
        } else if (*p == '(') {
            // Command substitution $(...)
            p++;
            int paren_count = 1;
            char cmd[MAX_TOKEN_LEN] = {0};
            int cmd_pos = 0;
            while (*p && paren_count > 0 && cmd_pos < MAX_TOKEN_LEN - 1) {
                if (*p == '(') paren_count++;
                else if (*p == ')') paren_count--;
                if (paren_count > 0) {
                    cmd[cmd_pos++] = *p;
                }
                p++;
            }
            cmd[cmd_pos] = '\0';
            if (paren_count == 0) {
                if (*pos < max_len - 1) current_token[(*pos)++] = '$';
                if (*pos < max_len - 1) current_token[(*pos)++] = '(';
                for (int i = 0; cmd[i] && *pos < max_len - 1; i++) {
                    current_token[(*pos)++] = cmd[i];
                }
                if (*pos < max_len - 1) current_token[(*pos)++] = ')';
            } else {
                fprintf(stderr, "Error: Unmatched ')' in command substitution\n");
                return p; // Avoid advancing past error
            }
            return p;
        } else if (isalpha(*p) || *p == '_' || strchr("*@#?$!-0", *p)) {
            char var_name[MAX_TOKEN_LEN] = {0};
            int var_pos = 0;
            var_name[var_pos++] = *p++;
            if (var_name[0] == '*' || var_name[0] == '@' || var_name[0] == '#' ||
                var_name[0] == '?' || var_name[0] == '!' || var_name[0] == '-' ||
                var_name[0] == '$' || var_name[0] == '0') {
                // Special parameter
            } else {
                while (*p && (isalnum(*p) || *p == '_') && var_pos < MAX_TOKEN_LEN - 1) {
                    var_name[var_pos++] = *p++;
                }
            }
            var_name[var_pos] = '\0';
            if (*pos < max_len - 1) current_token[(*pos)++] = '$';
            for (int i = 0; var_name[i] && *pos < max_len - 1; i++) {
                current_token[(*pos)++] = var_name[i];
            }
            return p;
        } else {
            if (*pos < max_len - 1) current_token[(*pos)++] = '$';
            return p;
        }
    } else if (*p == '`') {
        // Command substitution `...`
        p++;
        char cmd[MAX_TOKEN_LEN] = {0};
        int cmd_pos = 0;
        int escaped = 0;
        while (*p && cmd_pos < MAX_TOKEN_LEN - 1) {
            if (escaped) {
                cmd[cmd_pos++] = *p;
                escaped = 0;
                p++;
                continue;
            }
            if (*p == '\\') {
                escaped = 1;
                p++;
                continue;
            }
            if (*p == '`') {
                p++;
                break;
            }
            cmd[cmd_pos++] = *p++;
        }
        cmd[cmd_pos] = '\0';
        if (*(p - 1) == '`' || *p == '`') {
            if (*pos < max_len - 1) current_token[(*pos)++] = '`';
            for (int i = 0; cmd[i] && *pos < max_len - 1; i++) {
                current_token[(*pos)++] = cmd[i];
            }
            if (*pos < max_len - 1) current_token[(*pos)++] = '`';
        } else {
            fprintf(stderr, "Error: Unmatched '`' in command substitution\n");
            return p;
        }
        return p;
    }
    return p;
}

void tokenize(const char *input, Token *tokens, int *token_count, int depth,
              char **active_aliases, int active_alias_count) {
    *token_count = 0;
    const char *p = input;
    char current_token[MAX_TOKEN_LEN];
    int pos = 0;
    int in_quoted = 0;
    int in_word = 0;
    int after_equals = 0; // Track for tilde after =

    // Helper macro to check MAX_TOKEN_LEN
    #define CHECK_TOKEN_LEN() do { \
        if (pos >= MAX_TOKEN_LEN - 1) { \
            fprintf(stderr, "Error: Token length exceeds %d characters\n", MAX_TOKEN_LEN - 1); \
            return; \
        } \
    } while (0)

    // Helper macro to check MAX_TOKENS
    #define CHECK_TOKEN_COUNT() do { \
        if (*token_count >= MAX_TOKENS) { \
            fprintf(stderr, "Error: Token count exceeds %d\n", MAX_TOKENS); \
            return; \
        } \
    } while (0)

    while (*p) {
        // Handle heredoc after << or <<-
        if (pos >= 2 && strncmp(current_token + pos - 2, "<<", 2) == 0 &&
            (current_token[pos - 2] == '<' || current_token[pos - 2] == '-')) {
            int is_dash = (current_token[pos - 2] == '-' && current_token[pos - 3] == '<');
            if (is_dash) {
                current_token[pos - 2] = '\0';
                pos -= 2;
            } else {
                current_token[pos - 2] = '\0';
                pos -= 2;
            }
            CHECK_TOKEN_COUNT();
            strncpy(tokens[*token_count].text, is_dash ? "<<-" : "<<", MAX_TOKEN_LEN);
            tokens[*token_count].type = is_dash ? DLESSDASH : DLESS;
            (*token_count)++;
            pos = 0;

            // Parse delimiter
            char delimiter[MAX_TOKEN_LEN] = {0};
            int delim_pos = 0;
            int delim_quoted = 0;
            if (*p == '\'' || *p == '"') {
                char quote = *p++;
                delim_quoted = 1;
                while (*p && *p != quote) {
                    if (delim_pos < MAX_TOKEN_LEN - 1) {
                        delimiter[delim_pos++] = *p++;
                    } else {
                        fprintf(stderr, "Error: Delimiter exceeds %d characters\n", MAX_TOKEN_LEN - 1);
                        return;
                    }
                }
                if (*p == quote) p++;
                else {
                    fprintf(stderr, "Error: Unmatched quote in heredoc delimiter\n");
                    return;
                }
            } else {
                while (*p && !isspace(*p) && *p != '\n' && !is_operator_start(*p)) {
                    if (delim_pos < MAX_TOKEN_LEN - 1) {
                        delimiter[delim_pos++] = *p++;
                    } else {
                        fprintf(stderr, "Error: Delimiter exceeds %d characters\n", MAX_TOKEN_LEN - 1);
                        return;
                    }
                }
            }
            delimiter[delim_pos] = '\0';

            // Skip whitespace after delimiter
            while (*p && isspace(*p) && *p != '\n') p++;
            if (*p == '\n') p++;

            // Collect heredoc content until delimiter
            char *content = malloc(512);
            int content_capacity = 512;
            int content_len = 0;
            content[0] = '\0';
            char line[MAX_TOKEN_LEN];
            int line_pos = 0;
            int found_delimiter = 0;

            while (*p && !found_delimiter) {
                if (*p == '\n' || !*p) {
                    line[line_pos] = '\0';
                    // Check if line matches delimiter
                    char *trimmed_line = line;
                    if (is_dash) {
                        while (*trimmed_line == '\t') trimmed_line++;
                    }
                    if (strcmp(trimmed_line, delimiter) == 0) {
                        found_delimiter = 1;
                        p++;
                        break;
                    }
                    // Append line to content
                    for (int i = 0; line[i]; i++) {
                        append_to_string(&content, &content_capacity, &content_len, line[i]);
                    }
                    append_to_string(&content, &content_capacity, &content_len, '\n');
                    line_pos = 0;
                    if (*p == '\n') p++;
                } else {
                    if (line_pos < MAX_TOKEN_LEN - 1) {
                        line[line_pos++] = *p++;
                    } else {
                        fprintf(stderr, "Error: Heredoc line exceeds %d characters\n", MAX_TOKEN_LEN - 1);
                        free(content);
                        return;
                    }
                }
            }

            if (!found_delimiter) {
                fprintf(stderr, "Error: Heredoc delimiter '%s' not found\n", delimiter);
                free(content);
                return;
            }

            // Store delimiter and content as a single token (for simplicity)
            CHECK_TOKEN_COUNT();
            snprintf(tokens[*token_count].text, MAX_TOKEN_LEN, "%s\n%s", delimiter, content);
            tokens[*token_count].type = WORD;
            (*token_count)++;
            free(content);
            continue;
        }

        // Handle tilde expansion
        if (*p == '~' && !in_quoted && (!in_word || after_equals)) {
            if (pos > 0) {
                current_token[pos] = '\0';
                CHECK_TOKEN_COUNT();
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                tokens[*token_count].type = WORD;
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            char tilde_text[MAX_TOKEN_LEN] = {0};
            int tilde_pos = 0;
            tilde_text[tilde_pos++] = *p++;
            if (*p && (isalpha(*p) || *p == '_')) {
                while (*p && (isalnum(*p) || *p == '_') && tilde_pos < MAX_TOKEN_LEN - 1) {
                    tilde_text[tilde_pos++] = *p++;
                }
            }
            tilde_text[tilde_pos] = '\0';
            CHECK_TOKEN_COUNT();
            strncpy(tokens[*token_count].text, tilde_text, MAX_TOKEN_LEN);
            tokens[*token_count].type = TILDE;
            (*token_count)++;
            after_equals = 0;
            continue;
        }

        if (*p == '#' && !in_word && !in_quoted) {
            while (*p && *p != '\n') p++;
            continue;
        }

        if (*p == '\n' && !in_quoted) {
            if (in_word) {
                current_token[pos] = '\0';
                CHECK_TOKEN_COUNT();
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                tokens[*token_count].type = WORD;
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            CHECK_TOKEN_COUNT();
            strncpy(tokens[*token_count].text, "\n", MAX_TOKEN_LEN);
            tokens[*token_count].type = NEWLINE;
            (*token_count)++;
            p++;
            after_equals = 0;
            continue;
        }

        if (*p == '\\' || *p == '\'' || *p == '"') {
            if (!in_word) in_word = 1;
            if (*p == '\\') {
                CHECK_TOKEN_LEN();
                current_token[pos++] = *p++;
                if (*p && *p != '\n') {
                    CHECK_TOKEN_LEN();
                    current_token[pos++] = *p++;
                } else if (*p == '\n') {
                    p++;
                }
                continue;
            }
            if (*p == '\'') {
                in_quoted = 1;
                CHECK_TOKEN_LEN();
                current_token[pos++] = *p++;
                while (*p && *p != '\'') {
                    CHECK_TOKEN_LEN();
                    current_token[pos++] = *p++;
                }
                if (*p == '\'') {
                    CHECK_TOKEN_LEN();
                    current_token[pos++] = *p++;
                    in_quoted = 0;
                } else {
                    fprintf(stderr, "Error: Unmatched single quote\n");
                    return;
                }
                continue;
            }
            if (*p == '"') {
                in_quoted = !in_quoted;
                CHECK_TOKEN_LEN();
                current_token[pos++] = *p++;
                continue;
            }
        }

        if (*p == '$' || *p == '`') {
            if (!in_word && !in_quoted) in_word = 1;
            p = parse_expansion(p, current_token, &pos, MAX_TOKEN_LEN, in_quoted, depth,
                               active_aliases, active_alias_count);
            continue;
        }

        if (isspace(*p) && *p != '\n' && !in_quoted) {
            if (in_word) {
                current_token[pos] = '\0';
                CHECK_TOKEN_COUNT();
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                tokens[*token_count].type = WORD;
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            p++;
            after_equals = 0;
            continue;
        }

        if (is_operator_start(*p)) {
            if (in_word) {
                current_token[pos] = '\0';
                CHECK_TOKEN_COUNT();
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                tokens[*token_count].type = WORD;
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            CHECK_TOKEN_LEN();
            current_token[pos++] = *p++;
            if (*p && (is_operator_start(*p) || *p == '-')) {
                CHECK_TOKEN_LEN();
                current_token[pos++] = *p++;
            }
            current_token[pos] = '\0';
            if (is_operator(current_token)) {
                CHECK_TOKEN_COUNT();
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                tokens[*token_count].type = get_operator_type(current_token);
                (*token_count)++;
                pos = 0;
            } else {
                pos = 0;
            }
            after_equals = 0;
            continue;
        }

        if (*p == '=') {
            after_equals = 1;
        } else if (!isspace(*p)) {
            after_equals = 0;
        }

        if (!in_word) in_word = 1;
        CHECK_TOKEN_LEN();
        current_token[pos++] = *p++;
    }

    if (in_word) {
        current_token[pos] = '\0';
        CHECK_TOKEN_COUNT();
        strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
        tokens[*token_count].type = WORD;
        (*token_count)++;
    }

    // Post-processing
    for (int i = 0; i < *token_count; i++) {
        if (is_operator(tokens[i].text)) {
            tokens[i].type = get_operator_type(tokens[i].text);
        } else if (i + 1 < *token_count && is_all_digits(tokens[i].text) &&
                   (tokens[i + 1].type == LESS || tokens[i + 1].type == GREAT ||
                    tokens[i + 1].type == DGREAT || tokens[i + 1].type == DLESS ||
                    tokens[i + 1].type == DLESSDASH || tokens[i + 1].type == LESSAND ||
                    tokens[i + 1].type == GREATAND || tokens[i + 1].type == LESSGREAT ||
                    tokens[i + 1].type == CLOBBER)) {
            tokens[i].type = IO_NUMBER;
        } else {
            tokens[i].type = tokens[i].type == TILDE ? TILDE : TOKEN;
        }
    }

    for (int i = 0; i < *token_count; i++) {
        if (tokens[i].type == TOKEN) {
            if (i == 0 || (i > 0 && (is_operator(tokens[i - 1].text) ||
                                     tokens[i - 1].type == NEWLINE))) {
                TokenType reserved = get_reserved_word_type(tokens[i].text);
                if (reserved != TOKEN) {
                    tokens[i].type = reserved;
                } else if (i + 1 < *token_count && (tokens[i + 1].type == DO ||
                                                  tokens[i + 1].type == LPAREN) &&
                           is_valid_name(tokens[i].text)) {
                    tokens[i].type = NAME;
                } else {
                    tokens[i].type = WORD;
                }
            } else if (i > 0 && tokens[i - 1].type != ASSIGNMENT_WORD &&
                       strchr(tokens[i].text, '=')) {
                char *eq = strchr(tokens[i].text, '=');
                if (eq != tokens[i].text) {
                    *eq = '\0';
                    if (is_valid_name(tokens[i].text)) {
                        tokens[i].type = ASSIGNMENT_WORD;
                    }
                    *eq = '=';
                }
                if (tokens[i].type == TOKEN) {
                    tokens[i].type = WORD;
                }
            } else {
                tokens[i].type = WORD;
            }
        }
    }

    // Alias substitution (unchanged)
    for (int i = 0; i < *token_count;) {
        char alias_value[MAX_TOKEN_LEN];
        Token temp_tokens[MAX_TOKENS];
        int temp_token_count = 0;
        int perform_alias_check = 1;

        if (tokens[i].type != WORD && tokens[i].type != NAME) {
            i++;
            continue;
        }

        if (depth >= MAX_ALIAS_DEPTH) {
            fprintf(stderr, "Error: Alias recursion limit exceeded\n");
            i++;
            continue;
        }

        for (int j = 0; j < active_alias_count; j++) {
            if (strcmp(active_aliases[j], tokens[i].text) == 0) {
                i++;
                perform_alias_check = 0;
                break;
            }
        }
        if (!perform_alias_check) {
            continue;
        }

        if (substitute_alias(tokens[i].text, alias_value, MAX_TOKEN_LEN)) {
            if (active_alias_count >= MAX_ALIAS_DEPTH) {
                fprintf(stderr, "Error: Too many active aliases\n");
                i++;
                continue;
            }
            active_aliases[active_alias_count] = tokens[i].text;
            int new_active_alias_count = active_alias_count + 1;

            tokenize(alias_value, temp_tokens, &temp_token_count, depth + 1, active_aliases, new_active_alias_count);

            int shift_amount = temp_token_count - 1;
            if (shift_amount > 0) {
                if (*token_count + shift_amount >= MAX_TOKENS) {
                    fprintf(stderr, "Error: Token count exceeds %d after alias expansion\n", MAX_TOKENS);
                    return;
                }
                for (int j = *token_count - 1; j > i; j--) {
                    tokens[j + shift_amount] = tokens[j];
                }
            }

            for (int j = 0; j < temp_token_count; j++) {
                tokens[i + j] = temp_tokens[j];
            }
            *token_count += shift_amount;

            if (temp_token_count > 0 && isspace(alias_value[strlen(alias_value) - 1])) {
                i += temp_token_count;
                perform_alias_check = 1;
            } else {
                i += temp_token_count;
                perform_alias_check = 0;
            }
        } else {
            i++;
            perform_alias_check = 0;
        }
    }

    #undef CHECK_TOKEN_LEN
    #undef CHECK_TOKEN_COUNT
}

void init_parser_state(ParserState *state) {
    state->token_capacity = MAX_TOKENS;
    state->tokens = malloc(state->token_capacity * sizeof(Token));
    state->token_count = 0;
    state->pos = 0;
    state->brace_depth = 0;
    state->paren_depth = 0;
    state->expecting = 0;
}

void free_parser_state(ParserState *state) {
    free(state->tokens);
    state->tokens = NULL;
    state->token_count = 0;
    state->token_capacity = 0;
    state->pos = 0;
}
static Redirect *parse_redirect_list(ParserState *state) {
    Redirect *head = NULL, *tail = NULL;
    while (state->pos < state->token_count && (state->tokens[state->pos].type == IO_NUMBER ||
           state->tokens[state->pos].type == LESS || state->tokens[state->pos].type == GREAT ||
           state->tokens[state->pos].type == DLESS || state->tokens[state->pos].type == DGREAT ||
           state->tokens[state->pos].type == LESSAND || state->tokens[state->pos].type == GREATAND ||
           state->tokens[state->pos].type == LESSGREAT || state->tokens[state->pos].type == CLOBBER ||
           state->tokens[state->pos].type == DLESSDASH)) {
        Redirect *redir = malloc(sizeof(Redirect));
        redir->io_number = NULL;
        redir->filename = NULL;
        redir->delimiter = NULL;
        redir->heredoc_content = NULL;
        redir->is_quoted = 0;
        redir->is_dash = 0;
        redir->next = NULL;

        if (state->tokens[state->pos].type == IO_NUMBER) {
            redir->io_number = strdup(state->tokens[state->pos].text);
            (state->pos)++;
        }

        if (state->pos >= state->token_count || (state->tokens[state->pos].type != LESS &&
            state->tokens[state->pos].type != GREAT && state->tokens[state->pos].type != DLESS &&
            state->tokens[state->pos].type != DGREAT && state->tokens[state->pos].type != LESSAND &&
            state->tokens[state->pos].type != GREATAND && state->tokens[state->pos].type != LESSGREAT &&
            state->tokens[state->pos].type != CLOBBER && state->tokens[state->pos].type != DLESSDASH)) {
            fprintf(stderr, "Error: Expected redirect operator after IO_NUMBER\n");
            free(redir->io_number);
            free(redir);
            while (head) {
                Redirect *next = head->next;
                free(head->io_number);
                free(head->filename);
                free(head->delimiter);
                free(head->heredoc_content);
                free(head);
                head = next;
            }
            return NULL;
        }

        redir->operator = state->tokens[state->pos].type;
        (state->pos)++;

        if (state->pos >= state->token_count || state->tokens[state->pos].type != WORD) {
            fprintf(stderr, "Error: Expected filename or delimiter after redirect operator\n");
            free(redir->io_number);
            free(redir);
            while (head) {
                Redirect *next = head->next;
                free(head->io_number);
                free(head->filename);
                free(head->delimiter);
                free(head->heredoc_content);
                free(head);
                head = next;
            }
            return NULL;
        }

        if (redir->operator == DLESS || redir->operator == DLESSDASH) {
            redir->is_dash = (redir->operator == DLESSDASH);
            // Split token into delimiter and content
            char *token_text = state->tokens[state->pos].text;
            char *nl = strchr(token_text, '\n');
            if (!nl) {
                fprintf(stderr, "Error: Invalid heredoc format\n");
                free(redir->io_number);
                free(redir);
                while (head) {
                    Redirect *next = head->next;
                    free(head->io_number);
                    free(head->filename);
                    free(head->delimiter);
                    free(head->heredoc_content);
                    free(head);
                    head = next;
                }
                return NULL;
            }
            *nl = '\0';
            redir->delimiter = strdup(token_text);
            redir->heredoc_content = strdup(nl + 1);
            *nl = '\n';
            // Check if delimiter was quoted (simplified, assumes tokenizer tracked it)
            redir->is_quoted = (token_text[0] == '\'' || token_text[0] == '"');
        } else {
            redir->filename = strdup(state->tokens[state->pos].text);
        }
        (state->pos)++;

        if (!head) {
            head = tail = redir;
        } else {
            tail->next = redir;
            tail = redir;
        }
    }
    return head;
}

static ASTNode *parse_simple_command(ParserState *state) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_SIMPLE_COMMAND;
    node->data.simple_command.prefix = NULL;
    node->data.simple_command.prefix_count = 0;
    node->data.simple_command.command = NULL;
    node->data.simple_command.suffix = NULL;
    node->data.simple_command.suffix_count = 0;
    node->data.simple_command.expansions = NULL;
    node->data.simple_command.expansion_count = 0;
    node->data.simple_command.redirects = NULL;

    // Parse prefix (assignments)
    while (state->pos < state->token_count && state->tokens[state->pos].type == ASSIGNMENT_WORD) {
        node->data.simple_command.prefix = realloc(node->data.simple_command.prefix,
                                                  (node->data.simple_command.prefix_count + 1) * sizeof(char *));
        node->data.simple_command.prefix[node->data.simple_command.prefix_count++] =
            strdup(state->tokens[state->pos].text);
        state->pos++;
    }

    // Parse expansions and words
    while (state->pos < state->token_count &&
           (state->tokens[state->pos].type == WORD || state->tokens[state->pos].type == TILDE)) {
        if (state->tokens[state->pos].type == TILDE) {
            // Handle tilde expansion
            node->data.simple_command.expansions = realloc(node->data.simple_command.expansions,
                                                         (node->data.simple_command.expansion_count + 1) * sizeof(Expansion *));
            Expansion *exp = malloc(sizeof(Expansion));
            exp->type = EXPANSION_TILDE;
            exp->data.tilde.user = (state->tokens[state->pos].text[1] != '\0') ?
                                   strdup(state->tokens[state->pos].text + 1) : NULL;
            node->data.simple_command.expansions[node->data.simple_command.expansion_count++] = exp;
            state->pos++;
            continue;
        }

        char *text = state->tokens[state->pos].text;
        if (text[0] == '$' || text[0] == '`') {
            node->data.simple_command.expansions = realloc(node->data.simple_command.expansions,
                                                         (node->data.simple_command.expansion_count + 1) * sizeof(Expansion *));
            Expansion *exp = malloc(sizeof(Expansion));
            if (text[0] == '`') {
                // Backtick command substitution
                exp->type = EXPANSION_COMMAND;
                if (strlen(text) <= 2) {
                    // Empty ` `
                    exp->data.command.command = NULL;
                } else {
                    char *cmd = strdup(text + 1);
                    cmd[strlen(cmd) - 1] = '\0'; // Remove trailing `
                    ParserState sub_state;
                    init_parser_state(&sub_state);
                    tokenize(cmd, sub_state.tokens, &sub_state.token_count, depth + 1, NULL, 0);
                    sub_state.pos = 0;
                    ASTNode *sub_ast = NULL;
                    ParseStatus status = parse_line_internal(&sub_state, &sub_ast);
                    exp->data.command.command = (status == PARSE_COMPLETE && sub_ast) ? sub_ast : NULL;
                    free(cmd);
                    free_parser_state(&sub_state);
                }
            } else if (strncmp(text, "$(", 2) == 0) {
                // $(...) command substitution
                exp->type = EXPANSION_COMMAND;
                if (strlen(text) <= 3) {
                    // Empty $()
                    exp->data.command.command = NULL;
                } else {
                    char *cmd = strdup(text + 2);
                    cmd[strlen(cmd) - 1] = '\0'; // Remove )
                    ParserState sub_state;
                    init_parser_state(&sub_state);
                    tokenize(cmd, sub_state.tokens, &sub_state.token_count, depth + 1, NULL, 0);
                    sub_state.pos = 0;
                    ASTNode *sub_ast = NULL;
                    ParseStatus status = parse_line_internal(&sub_state, &sub_ast);
                    exp->data.command.command = (status == PARSE_COMPLETE && sub_ast) ? sub_ast : NULL;
                    free(cmd);
                    free_parser_state(&sub_state);
                }
            } else if (strncmp(text, "$((", 3) == 0) {
                exp->type = EXPANSION_ARITHMETIC;
                char *expr = strdup(text + 3);
                expr[strlen(expr) - 2] = '\0'; // Remove ))
                exp->data.arithmetic.expression = expr;
            } else if (strncmp(text, "${", 2) == 0) {
                char *inner = text + 2;
                char *end = strchr(inner, '}');
                if (!end) {
                    fprintf(stderr, "Error: Invalid parameter expansion %s\n", text);
                    free(exp);
                    state->pos++;
                    continue;
                }
                *end = '\0';
                if (inner[0] == '#') {
                    if (inner[1] == '#') {
                        exp->type = EXPANSION_PREFIX_LONG;
                        exp->data.pattern.var = strdup(inner + 2);
                        exp->data.pattern.pattern = strdup(end + 1);
                    } else {
                        exp->type = EXPANSION_LENGTH;
                        exp->data.length.var = strdup(inner + 1);
                    }
                } else if (strchr(inner, ':')) {
                    char *colon = strchr(inner, ':');
                    *colon = '\0';
                    char op = colon[1];
                    char *rest = colon + 2;
                    if (op == '-' || op == '=') {
                        exp->type = (op == '-') ? EXPANSION_DEFAULT : EXPANSION_ASSIGN;
                        exp->data.default_exp.var = strdup(inner);
                        exp->data.default_exp.default_value = strdup(rest);
                        exp->data.default_exp.is_colon = 1;
                    } else if (isdigit(op) || op == '-') {
                        exp->type = EXPANSION_SUBSTRING;
                        exp->data.substring.var = strdup(inner);
                        char *len_sep = strchr(rest, ':');
                        if (len_sep) {
                            *len_sep = '\0';
                            exp->data.substring.offset = strdup(rest);
                            exp->data.substring.length = strdup(len_sep + 1);
                        } else {
                            exp->data.substring.offset = strdup(rest);
                            exp->data.substring.length = NULL;
                        }
                    }
                    *colon = ':';
                } else if (strchr(inner, '#') || strchr(inner, '%')) {
                    char *op = strchr(inner, '#') ? strchr(inner, '#') : strchr(inner, '%');
                    char op_char = *op;
                    *op = '\0';
                    if (op[1] == op_char) {
                        exp->type = (op_char == '#') ? EXPANSION_PREFIX_LONG : EXPANSION_SUFFIX_LONG;
                        op++;
                    } else {
                        exp->type = (op_char == '#') ? EXPANSION_PREFIX_SHORT : EXPANSION_SUFFIX_SHORT;
                    }
                    exp->data.pattern.var = strdup(inner);
                    exp->data.pattern.pattern = strdup(op + 1);
                    *op = op_char;
                } else {
                    exp->type = EXPANSION_PARAMETER;
                    exp->data.parameter.name = strdup(inner);
                }
                *end = '}';
            } else if (text[1] == '*' || text[1] == '@' || text[1] == '#' ||
                       text[1] == '?' || text[1] == '!' || text[1] == '-' ||
                       text[1] == '$' || text[1] == '0') {
                exp->type = EXPANSION_SPECIAL;
                exp->data.special.name = strdup(text + 1);
            } else {
                exp->type = EXPANSION_PARAMETER;
                exp->data.parameter.name = strdup(text + 1);
            }
            node->data.simple_command.expansions[node->data.simple_command.expansion_count++] = exp;
        } else {
            // Regular word
            if (!node->data.simple_command.command) {
                node->data.simple_command.command = strdup(text);
            } else {
                node->data.simple_command.suffix = realloc(node->data.simple_command.suffix,
                                                         (node->data.simple_command.suffix_count + 1) * sizeof(char *));
                node->data.simple_command.suffix[node->data.simple_command.suffix_count++] =
                    strdup(text);
            }
        }
        state->pos++;
    }

    // Parse redirects
    node->data.simple_command.redirects = parse_redirect_list(state);

    if (!node->data.simple_command.command &&
        node->data.simple_command.prefix_count == 0 &&
        node->data.simple_command.suffix_count == 0 &&
        node->data.simple_command.expansion_count == 0 &&
        !node->data.simple_command.redirects) {
        free_ast(node);
        return NULL;
    }
    return node;
}

static ASTNode *parse_compound_list(ParserState *state);

static ASTNode *parse_if_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != IF) {
        return NULL;
    }
    state->pos++; // Consume IF

    ASTNode *condition = parse_compound_list(state);
    if (!condition && state->pos < state->token_count && state->tokens[state->pos].type == THEN) {
        condition = malloc(sizeof(ASTNode));
        condition->type = AST_LIST;
        condition->data.list.and_or = NULL;
        condition->data.list.separator = SEMI;
        condition->data.list.next = NULL;
    }
    if (!condition) {
        fprintf(stderr, "Error: Expected condition after 'if'\n");
        return NULL;
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != THEN) {
        fprintf(stderr, "Error: Expected 'then' after condition\n");
        free_ast(condition);
        return NULL;
    }
    state->pos++; // Consume THEN

    ASTNode *then_body = parse_compound_list(state);
    if (!then_body) {
        // Allow empty then body
        then_body = malloc(sizeof(ASTNode));
        then_body->type = AST_LIST;
        then_body->data.list.and_or = NULL;
        then_body->data.list.separator = SEMI;
        then_body->data.list.next = NULL;
    }

    ASTNode *else_part = NULL;
    if (state->pos < state->token_count && state->tokens[state->pos].type == ELSE) {
        state->pos++; // Consume ELSE
        else_part = parse_compound_list(state);
        if (!else_part) {
            // Allow empty else part
            else_part = malloc(sizeof(ASTNode));
            else_part->type = AST_LIST;
            else_part->data.list.and_or = NULL;
            else_part->data.list.separator = SEMI;
            else_part->data.list.next = NULL;
        }
    } else if (state->pos < state->token_count && state->tokens[state->pos].type == ELIF) {
        state->pos++; // Consume ELIF
        else_part = parse_if_clause(state);
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != FI) {
        fprintf(stderr, "Error: Expected 'fi' to close 'if'\n");
        free_ast(condition);
        free_ast(then_body);
        free_ast(else_part);
        return NULL;
    }
    state->pos++; // Consume FI

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_IF_CLAUSE;
    node->data.if_clause.condition = condition;
    node->data.if_clause.then_body = then_body;
    node->data.if_clause.else_part = else_part;
    return node;
}

static ASTNode *parse_for_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != FOR) {
        return NULL;
    }
    state->pos++; // Consume FOR

    if (state->pos >= state->token_count || state->tokens[state->pos].type != NAME) {
        fprintf(stderr, "Error: Expected variable name after 'for'\n");
        return NULL;
    }
    char *variable = strdup(state->tokens[state->pos].text);
    state->pos++; // Consume NAME

    char **wordlist = NULL;
    int wordlist_count = 0;
    if (state->pos < state->token_count && state->tokens[state->pos].type == IN) {
        state->pos++; // Consume IN
        while (state->pos < state->token_count && state->tokens[state->pos].type == WORD) {
            wordlist = realloc(wordlist, (wordlist_count + 1) * sizeof(char *));
            wordlist[wordlist_count++] = strdup(state->tokens[state->pos].text);
            state->pos++;
        }
        // Consume separator (;, newline)
        while (state->pos < state->token_count &&
               (state->tokens[state->pos].type == SEMI || state->tokens[state->pos].type == NEWLINE)) {
            state->pos++;
        }
    } else {
        // Default wordlist (e.g., "$@")
        while (state->pos < state->token_count &&
               (state->tokens[state->pos].type == SEMI || state->tokens[state->pos].type == NEWLINE)) {
            state->pos++;
        }
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != DO) {
        fprintf(stderr, "Error: Expected 'do' after 'for'\n");
        free(variable);
        for (int i = 0; i < wordlist_count; i++) free(wordlist[i]);
        free(wordlist);
        return NULL;
    }
    state->pos++; // Consume DO

    ASTNode *body = parse_compound_list(state);
    if (!body) {
        // Allow empty body
        body = malloc(sizeof(ASTNode));
        body->type = AST_LIST;
        body->data.list.and_or = NULL;
        body->data.list.separator = SEMI;
        body->data.list.next = NULL;
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != DONE) {
        fprintf(stderr, "Error: Expected 'done' to close 'for'\n");
        free(variable);
        for (int i = 0; i < wordlist_count; i++) free(wordlist[i]);
        free(wordlist);
        free_ast(body);
        return NULL;
    }
    state->pos++; // Consume DONE

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_FOR_CLAUSE;
    node->data.for_clause.variable = variable;
    node->data.for_clause.wordlist = wordlist;
    node->data.for_clause.wordlist_count = wordlist_count;
    node->data.for_clause.body = body;
    return node;
}

static CaseItem *parse_case_item(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != LPAREN) {
        return NULL;
    }
    state->pos++; // Consume LPAREN

    char **patterns = NULL;
    int pattern_count = 0;
    while (state->pos < state->token_count && state->tokens[state->pos].type == WORD) {
        patterns = realloc(patterns, (pattern_count + 1) * sizeof(char *));
        patterns[pattern_count++] = strdup(state->tokens[state->pos].text);
        state->pos++;
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != RPAREN) {
        fprintf(stderr, "Error: Expected ')' after case pattern\n");
        for (int i = 0; i < pattern_count; i++) free(patterns[i]);
        free(patterns);
        return NULL;
    }
    state->pos++; // Consume RPAREN

    ASTNode *action = parse_compound_list(state);
    if (!action) {
        // Allow empty action
        action = malloc(sizeof(ASTNode));
        action->type = AST_LIST;
        action->data.list.and_or = NULL;
        action->data.list.separator = SEMI;
        action->data.list.next = NULL;
    }

    int has_dsemi = 0;
    if (state->pos < state->token_count && state->tokens[state->pos].type == DSEMI) {
        has_dsemi = 1;
        state->pos++; // Consume DSEMI
    }

    CaseItem *item = malloc(sizeof(CaseItem));
    item->patterns = patterns;
    item->pattern_count = pattern_count;
    item->action = action;
    item->has_dsemi = has_dsemi;
    item->next = NULL;
    return item;
}

static ASTNode *parse_case_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != CASE) {
        return NULL;
    }
    state->pos++; // Consume CASE

    if (state->pos >= state->token_count || state->tokens[state->pos].type != WORD) {
        fprintf(stderr, "Error: Expected word after 'case'\n");
        return NULL;
    }
    char *word = strdup(state->tokens[state->pos].text);
    state->pos++; // Consume WORD

    if (state->pos >= state->token_count || state->tokens[state->pos].type != IN) {
        fprintf(stderr, "Error: Expected 'in' after case word\n");
        free(word);
        return NULL;
    }
    state->pos++; // Consume IN

    // Skip newlines before items
    while (state->pos < state->token_count && state->tokens[state->pos].type == NEWLINE) {
        state->pos++;
    }

    CaseItem *items = NULL, *tail = NULL;
    while (state->pos < state->token_count && state->tokens[state->pos].type != ESAC) {
        CaseItem *item = parse_case_item(state);
        if (!item) {
            // Allow empty item (e.g., ;; alone)
            if (state->pos < state->token_count && state->tokens[state->pos].type == DSEMI) {
                item = malloc(sizeof(CaseItem));
                item->patterns = NULL;
                item->pattern_count = 0;
                item->action = malloc(sizeof(ASTNode));
                item->action->type = AST_LIST;
                item->action->data.list.and_or = NULL;
                item->action->data.list.separator = SEMI;
                item->action->data.list.next = NULL;
                item->has_dsemi = 1;
                item->next = NULL;
                state->pos++; // Consume DSEMI
            } else {
                break;
            }
        }
        if (!items) {
            items = tail = item;
        } else {
            tail->next = item;
            tail = item;
        }
        // Skip extra DSEMI
        while (state->pos < state->token_count && state->tokens[state->pos].type == DSEMI) {
            state->pos++;
        }
        // Skip newlines
        while (state->pos < state->token_count && state->tokens[state->pos].type == NEWLINE) {
            state->pos++;
        }
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != ESAC) {
        fprintf(stderr, "Error: Expected 'esac' to close 'case'\n");
        free(word);
        while (items) {
            CaseItem *next = items->next;
            for (int i = 0; i < items->pattern_count; i++) free(items->patterns[i]);
            free(items->patterns);
            free_ast(items->action);
            free(items);
            items = next;
        }
        return NULL;
    }
    state->pos++; // Consume ESAC

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_CASE_CLAUSE;
    node->data.case_clause.word = word;
    node->data.case_clause.items = items;
    return node;
}

static ASTNode *parse_while_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != WHILE) {
        return NULL;
    }
    state->pos++; // Consume WHILE

    ASTNode *condition = parse_compound_list(state);
    if (!condition) {
        condition = malloc(sizeof(ASTNode));
        condition->type = AST_LIST;
        condition->data.list.and_or = NULL;
        condition->data.list.separator = SEMI;
        condition->data.list.next = NULL;
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != DO) {
        fprintf(stderr, "Error: Expected 'do' after while condition\n");
        free_ast(condition);
        return NULL;
    }
    state->pos++; // Consume DO

    ASTNode *body = parse_compound_list(state);
    if (!body) {
        body = malloc(sizeof(ASTNode));
        body->type = AST_LIST;
        body->data.list.and_or = NULL;
        body->data.list.separator = SEMI;
        body->data.list.next = NULL;
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != DONE) {
        fprintf(stderr, "Error: Expected 'done' to close 'while'\n");
        free_ast(condition);
        free_ast(body);
        return NULL;
    }
    state->pos++; // Consume DONE

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_WHILE_CLAUSE;
    node->data.while_clause.condition = condition;
    node->data.while_clause.body = body;
    return node;
}

static ASTNode *parse_until_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != UNTIL) {
        return NULL;
    }
    state->pos++; // Consume UNTIL

    ASTNode *condition = parse_compound_list(state);
    if (!condition) {
        // Allow empty condition (e.g., until; do ...)
        condition = malloc(sizeof(ASTNode));
        condition->type = AST_LIST;
        condition->data.list.and_or = NULL;
        condition->data.list.separator = SEMI;
        condition->data.list.next = NULL;
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != DO) {
        fprintf(stderr, "Error: Expected 'do' after until condition\n");
        free_ast(condition);
        return NULL;
    }
    state->pos++; // Consume DO

    ASTNode *body = parse_compound_list(state);
    if (!body) {
        // Allow empty body (e.g., do; done)
        body = malloc(sizeof(ASTNode));
        body->type = AST_LIST;
        body->data.list.and_or = NULL;
        body->data.list.separator = SEMI;
        body->data.list.next = NULL;
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != DONE) {
        fprintf(stderr, "Error: Expected 'done' to close 'until'\n");
        free_ast(condition);
        free_ast(body);
        return NULL;
    }
    state->pos++; // Consume DONE

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_UNTIL_CLAUSE;
    node->data.until_clause.condition = condition;
    node->data.until_clause.body = body;
    return node;
}

static ASTNode *parse_brace_group(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != LBRACE) return NULL;
    (state->pos)++;
    state->brace_depth++;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_BRACE_GROUP;
    node->data.brace_group.body = parse_compound_list(state);

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_RBRACE;
        return node;
    }
    if (state->tokens[state->pos].type != RBRACE) {
        fprintf(stderr, "Error: Expected '}' in brace group\n");
        state->expecting |= EXPECTING_RBRACE;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;
    state->brace_depth--;
    state->expecting &= ~EXPECTING_RBRACE;

    return node;
}

static ASTNode *parse_subshell(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != LPAREN) return NULL;
    (state->pos)++;
    state->paren_depth++;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_SUBSHELL;
    node->data.subshell.body = parse_compound_list(state);

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_RPAREN;
        return node;
    }
    if (state->tokens[state->pos].type != RPAREN) {
        fprintf(stderr, "Error: Expected ')' in subshell\n");
        state->expecting |= EXPECTING_RPAREN;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;
    state->paren_depth--;
    state->expecting &= ~EXPECTING_RPAREN;

    return node;
}

static ASTNode *parse_function_definition(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != NAME || 
        state->pos + 1 >= state->token_count || state->tokens[state->pos + 1].type != LPAREN) return NULL;
    
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_FUNCTION_DEFINITION;
    node->data.function_definition.name = strdup(state->tokens[state->pos].text);
    node->data.function_definition.redirects = NULL;
    (state->pos)++; // Skip NAME
    (state->pos)++; // Skip (

    if (state->pos >= state->token_count || state->tokens[state->pos].type != RPAREN) {
        fprintf(stderr, "Error: Expected ')' in function definition\n");
        state->expecting |= EXPECTING_RBRACE; // Assuming brace group follows
        free_ast(node);
        return NULL;
    }
    (state->pos)++;

    node->data.function_definition.body = parse_compound_list(state);

    if (state->pos >= state->token_count && (state->brace_depth > 0 || state->paren_depth > 0)) {
        state->expecting |= EXPECTING_RBRACE;
        return node;
    }

    if (state->pos < state->token_count && (state->tokens[state->pos].type == IO_NUMBER || state->tokens[state->pos].type == LESS || 
        state->tokens[state->pos].type == GREAT || state->tokens[state->pos].type == DLESS || state->tokens[state->pos].type == DGREAT || 
        state->tokens[state->pos].type == LESSAND || state->tokens[state->pos].type == GREATAND || state->tokens[state->pos].type == LESSGREAT || 
        state->tokens[state->pos].type == CLOBBER)) {
        node->data.function_definition.redirects = parse_redirect_list(state);
    }

    return node;
}

static ASTNode *parse_compound_command(ParserState *state) {
    if (state->pos >= state->token_count) return NULL;
    if (state->tokens[state->pos].type == LBRACE) return parse_brace_group(state);
    if (state->tokens[state->pos].type == LPAREN) return parse_subshell(state);
    if (state->tokens[state->pos].type == FOR) return parse_for_clause(state);
    if (state->tokens[state->pos].type == CASE) return parse_case_clause(state);
    if (state->tokens[state->pos].type == IF) return parse_if_clause(state);
    if (state->tokens[state->pos].type == WHILE) return parse_while_clause(state);
    if (state->tokens[state->pos].type == UNTIL) return parse_until_clause(state);
    return NULL;
}

static ASTNode *parse_command(ParserState *state) {
    ASTNode *cmd = NULL;
    if (state->pos < state->token_count && state->tokens[state->pos].type == NAME && 
        state->pos + 1 < state->token_count && state->tokens[state->pos + 1].type == LPAREN) {
        cmd = parse_function_definition(state);
    } else {
        cmd = parse_compound_command(state);
        if (!cmd) {
            cmd = parse_simple_command(state);
        }
    }

    if (cmd && state->pos < state->token_count && (state->tokens[state->pos].type == IO_NUMBER || state->tokens[state->pos].type == LESS || 
        state->tokens[state->pos].type == GREAT || state->tokens[state->pos].type == DLESS || state->tokens[state->pos].type == DGREAT || 
        state->tokens[state->pos].type == LESSAND || state->tokens[state->pos].type == GREATAND || state->tokens[state->pos].type == LESSGREAT || 
        state->tokens[state->pos].type == CLOBBER)) {
        Redirect *redir = parse_redirect_list(state);
        if (cmd->type == AST_SIMPLE_COMMAND) {
            cmd->data.simple_command.redirects = redir;
        } else if (cmd->type == AST_FUNCTION_DEFINITION) {
            cmd->data.function_definition.redirects = redir;
        }
    }

    return cmd;
}

static ASTNode *parse_pipeline(ParserState *state) {
    int bang = 0;
    if (state->pos < state->token_count && state->tokens[state->pos].type == BANG) {
        bang = 1;
        state->pos++; // Consume BANG
    }

    ASTNode **commands = NULL;
    int command_count = 0;

    // Parse first command
    ASTNode *cmd = parse_command(state);
    if (!cmd) {
        if (state->pos < state->token_count && state->tokens[state->pos].type == PIPE) {
            fprintf(stderr, "Error: Pipeline cannot start with '|'\n");
            return NULL;
        }
        return NULL;
    }
    commands = realloc(commands, (command_count + 1) * sizeof(ASTNode *));
    commands[command_count++] = cmd;

    // Parse additional commands after |
    while (state->pos < state->token_count && state->tokens[state->pos].type == PIPE) {
        state->pos++; // Consume PIPE
        cmd = parse_command(state);
        if (!cmd) {
            fprintf(stderr, "Error: Expected command after '|'\n");
            for (int i = 0; i < command_count; i++) {
                free_ast(commands[i]);
            }
            free(commands);
            return NULL;
        }
        commands = realloc(commands, (command_count + 1) * sizeof(ASTNode *));
        commands[command_count++] = cmd;
    }

    if (command_count == 0) {
        fprintf(stderr, "Error: Empty pipeline\n");
        free(commands);
        return NULL;
    }

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_PIPELINE;
    node->data.pipeline.commands = commands;
    node->data.pipeline.command_count = command_count;
    node->data.pipeline.bang = bang;
    return node;
}

static ASTNode *parse_and_or(ParserState *state) {
    ASTNode *left = parse_pipeline(state);
    if (!left || state->pos >= state->token_count || (state->tokens[state->pos].type != AND_IF && state->tokens[state->pos].type != OR_IF)) {
        return left;
    }

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_AND_OR;
    node->data.and_or.left = left;
    node->data.and_or.operator = state->tokens[state->pos].type;
    (state->pos)++;
    node->data.and_or.right = parse_pipeline(state);

    return node;
}

static ASTNode *parse_term(ParserState *state) {
    ASTNode *and_or = parse_and_or(state);
    if (!and_or || state->pos >= state->token_count || (state->tokens[state->pos].type != SEMI && state->tokens[state->pos].type != AMP)) {
        return and_or;
    }

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_LIST;
    node->data.list.and_or = and_or;
    node->data.list.separator = state->tokens[state->pos].type;
    (state->pos)++;
    node->data.list.next = parse_term(state);

    return node;
}

static ASTNode *parse_compound_list(ParserState *state) {
    ASTNode *head = NULL, *tail = NULL;
    int has_content = 0;

    while (state->pos < state->token_count) {
        // Skip newlines and comments (handled by tokenizer)
        while (state->pos < state->token_count && state->tokens[state->pos].type == NEWLINE) {
            state->pos++;
        }

        // Check for end of compound list (e.g., fi, done, esac)
        if (state->pos >= state->token_count ||
            state->tokens[state->pos].type == FI ||
            state->tokens[state->pos].type == DONE ||
            state->tokens[state->pos].type == ESAC ||
            state->tokens[state->pos].type == RBRACE) {
            break;
        }

        // Parse and_or (includes pipelines, commands)
        ASTNode *and_or = parse_and_or(state);
        if (!and_or && state->pos < state->token_count &&
            (state->tokens[state->pos].type == SEMI || state->tokens[state->pos].type == AMP)) {
            // Empty command before separator (e.g., ;; or &)
            and_or = NULL;
        } else if (!and_or) {
            // Parse error or end of input
            break;
        }

        has_content = 1;
        ASTNode *list_node = malloc(sizeof(ASTNode));
        list_node->type = AST_LIST;
        list_node->data.list.and_or = and_or;
        list_node->data.list.separator = SEMI; // Default
        list_node->data.list.next = NULL;

        // Handle separators
        while (state->pos < state->token_count &&
               (state->tokens[state->pos].type == SEMI || state->tokens[state->pos].type == AMP ||
                state->tokens[state->pos].type == NEWLINE)) {
            if (state->tokens[state->pos].type == AMP) {
                list_node->data.list.separator = AMP;
            }
            state->pos++;
        }

        if (!head) {
            head = tail = list_node;
        } else {
            tail->data.list.next = list_node;
            tail = list_node;
        }
    }

    // If no content (e.g., empty do; done), return null or empty list
    if (!has_content) {
        ASTNode *empty_list = malloc(sizeof(ASTNode));
        empty_list->type = AST_LIST;
        empty_list->data.list.and_or = NULL;
        empty_list->data.list.separator = SEMI;
        empty_list->data.list.next = NULL;
        return empty_list;
    }

    return head;
}

static ASTNode *parse_list(ParserState *state) {
    ASTNode *head = NULL, *tail = NULL;

    while (state->pos < state->token_count) {
        ASTNode *term = parse_term(state);
        if (!term && state->pos < state->token_count &&
            (state->tokens[state->pos].type == SEMI || state->tokens[state->pos].type == AMP)) {
            // Empty term (e.g., ;; or &)
            term = malloc(sizeof(ASTNode));
            term->type = AST_LIST;
            term->data.list.and_or = NULL;
            term->data.list.separator = state->tokens[state->pos].type;
            term->data.list.next = NULL;
        }
        if (!term) {
            break;
        }

        if (!head) {
            head = tail = term;
        } else {
            tail->data.list.next = term;
            tail = term;
        }

        // Consume multiple separators
        while (state->pos < state->token_count &&
               (state->tokens[state->pos].type == SEMI || state->tokens[state->pos].type == AMP)) {
            tail->data.list.separator = state->tokens[state->pos].type;
            state->pos++;
        }
    }

    return head;
}

static ASTNode *parse_complete_command(ParserState *state) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_COMPLETE_COMMAND;
    node->data.complete_command.list = parse_list(state);
    if (!node->data.complete_command.list) {
        free(node);
        return NULL;
    }
    node->data.complete_command.separator = (state->pos < state->token_count && 
        (state->tokens[state->pos].type == SEMI || state->tokens[state->pos].type == AMP)) ? state->tokens[state->pos].type : TOKEN;
    if (node->data.complete_command.separator != TOKEN) (state->pos)++;
    return node;
}

static ASTNode *parse_complete_commands(ParserState *state) {
    ASTNode *first = parse_complete_command(state);
    if (!first || state->pos >= state->token_count || state->tokens[state->pos].type != NEWLINE) return first;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_LIST;
    node->data.list.and_or = first;
    node->data.list.separator = NEWLINE;
    while (state->pos < state->token_count && state->tokens[state->pos].type == NEWLINE) (state->pos)++;
    node->data.list.next = parse_complete_commands(state);
    return node;
}

#define MAX_ALIAS_DEPTH 100

ParseStatus parse_line(const char *line, ParserState *state, ASTNode **ast) {
    char *active_aliases[MAX_ALIAS_DEPTH];
    Token new_tokens[MAX_TOKENS];
    int new_token_count = 0;

    tokenize(line, new_tokens, &new_token_count, 0, active_aliases, 0);

    if (state->token_count + new_token_count > state->token_capacity) {
        state->token_capacity = (state->token_count + new_token_count) * 2;
        state->tokens = realloc(state->tokens, state->token_capacity * sizeof(Token));
    }
    memcpy(state->tokens + state->token_count, new_tokens, new_token_count * sizeof(Token));
    state->token_count += new_token_count;

    state->pos = 0;

    while (state->pos < state->token_count && state->tokens[state->pos].type == NEWLINE) state->pos++;

    if (state->pos >= state->token_count && state->expecting == 0) {
        *ast = NULL;
        return PARSE_COMPLETE;
    }

    *ast = malloc(sizeof(ASTNode));
    (*ast)->type = AST_PROGRAM;
    (*ast)->data.program.commands = parse_complete_commands(state);

    if (state->pos < state->token_count) {
        fprintf(stderr, "Error: Incomplete parsing, extra tokens remain\n");
        free_ast(*ast);
        *ast = NULL;
        return PARSE_ERROR;
    }

    if (state->brace_depth > 0 || state->paren_depth > 0 || state->expecting != 0) {
        return PARSE_INCOMPLETE;
    }

    return PARSE_COMPLETE;
}

void init_environment(Environment *env, const char *shell_name, int argc, char *argv[]) {
    env->var_capacity = 16;
    env->variables = malloc(env->var_capacity * sizeof(char *));
    env->var_count = 0;
    env->shell_name = strdup(shell_name ? shell_name : "sh23");
    env->arg_count = argc > 0 ? argc - 1 : 0; // Exclude program name
    env->args = NULL;
    if (env->arg_count > 0) {
        env->args = malloc(env->arg_count * sizeof(char *));
        for (int i = 0; i < env->arg_count; i++) {
            env->args[i] = strdup(argv[i + 1]); // Copy argv[1] and up
        }
    }
}

void free_environment(Environment *env) {
    for (int i = 0; i < env->var_count; i++) free(env->variables[i].name);
    free(env->variables);
    free(env->shell_name);
    for (int i = 0; i < env->arg_count; i++) free(env->args[i]);
    free(env->args);
}

char *expand_tilde(const char *value) {
    if (value && value[0] == '~') {
        return strdup(value); // Placeholder: no HOME in C23
    }
    return strdup(value ? value : "");
}

static char *expand_parameter(Expansion *exp, Environment *env, FunctionTable *ft, int *last_exit_status) {
    switch (exp->type) {
        case EXPANSION_PARAMETER: {
            const char *value = get_variable(env, exp->data.parameter.name);
            return value ? strdup(value) : strdup("");
        }
        case EXPANSION_SPECIAL: {
            if (strcmp(exp->data.special.name, "?") == 0) {
                char status_str[16];
                snprintf(status_str, sizeof(status_str), "%d", *last_exit_status);
                return strdup(status_str);
            } else if (strcmp(exp->data.special.name, "0") == 0) {
                return strdup(env->shell_name);
            } else if (strcmp(exp->data.special.name, "#") == 0) {
                char count_str[16];
                snprintf(count_str, sizeof(count_str), "%d", env->arg_count);
                return strdup(count_str);
            } else if (isdigit(exp->data.special.name[0])) {
                int idx = atoi(exp->data.special.name);
                if (idx > 0 && idx <= env->arg_count) {
                    return strdup(env->args[idx]);
                }
                return strdup("");
            }
            fprintf(stderr, "Error: Special parameter $%s not supported by sh23\n", exp->data.special.name);
            *last_exit_status = 1;
            return strdup("");
        }
        case EXPANSION_DEFAULT:
        case EXPANSION_ASSIGN:
        case EXPANSION_SUBSTRING:
        case EXPANSION_LENGTH:
        case EXPANSION_PREFIX_SHORT:
        case EXPANSION_PREFIX_LONG:
        case EXPANSION_SUFFIX_SHORT:
        case EXPANSION_SUFFIX_LONG:
            fprintf(stderr, "Error: Advanced parameter expansion not supported by sh23\n");
            *last_exit_status = 1;
            return strdup("");
        case EXPANSION_COMMAND:
            fprintf(stderr, "Error: Command substitution not supported by sh23\n");
            *last_exit_status = 1;
            return strdup("");
        case EXPANSION_ARITHMETIC:
            fprintf(stderr, "Error: Arithmetic expansion not supported by sh23\n");
            *last_exit_status = 1;
            return strdup("");
        case EXPANSION_TILDE:
            fprintf(stderr, "Error: Tilde expansion not supported by sh23\n");
            *last_exit_status = 1;
            return strdup("");
        default:
            fprintf(stderr, "Error: Unknown expansion type\n");
            *last_exit_status = 1;
            return strdup("");
    }
}

char *expand_assignment(const char *assignment, Environment *env, FunctionTable *ft, int *last_exit_status) {
    char *equals = strchr(assignment, '=');
    if (!equals) return strdup(assignment);
    size_t name_len = equals - assignment;
    char name[name_len + 1];
    strncpy(name, assignment, name_len);
    name[name_len] = '\0';
    const char *value = equals + 1;

    char *expanded = expand_tilde(value);
    char *temp = expanded;

    if (strchr(expanded, '$') && strncmp(expanded, "$((", 3) != 0) {
        free(expanded);
        expanded = expand_parameter(temp, env, last_exit_status);
        free(temp);
        temp = expanded;
    }

    if (strncmp(expanded, "$(", 2) == 0 || strchr(expanded, '`')) {
        free(expanded);
        expanded = expand_command_substitution(temp, env, ft, last_exit_status);
        free(temp);
        temp = expanded;
    }

    if (strncmp(expanded, "$((", 3) == 0) {
        free(expanded);
        expanded = expand_arithmetic(temp, env);
        free(temp);
        temp = expanded;
    }

    expanded = remove_quotes(temp);
    free(temp);

    char *result = malloc(strlen(name) + strlen(expanded) + 2);
    sprintf(result, "%s=%s", name, expanded);
    free(expanded);
    return result;
}


void init_function_table(FunctionTable *ft) {
    ft->func_capacity = 16;
    ft->functions = malloc(ft->func_capacity * sizeof(Function));
    ft->func_count = 0;
}

void free_function_table(FunctionTable *ft) {
    for (int i = 0; i < ft->func_count; i++) {
        free(ft->functions[i].name);
        free_ast(ft->functions[i].body);
        Redirect *redir = ft->functions[i].redirects;
        while (redir) {
            free(redir->io_number);
            free(redir->filename);
            Redirect *next = redir->next;
            free(redir);
            redir = next;
        }
    }
    free(ft->functions);
}

void add_function(FunctionTable *ft, const char *name, ASTNode *body, Redirect *redirects) {
    for (int i = 0; i < ft->func_count; i++) {
        if (strcmp(ft->functions[i].name, name) == 0) {
            free(ft->functions[i].name);
            free_ast(ft->functions[i].body);
            Redirect *redir = ft->functions[i].redirects;
            while (redir) {
                free(redir->io_number);
                free(redir->filename);
                Redirect *next = redir->next;
                free(redir);
                redir = next;
            }
            ft->functions[i].name = strdup(name);
            ft->functions[i].body = body;
            ft->functions[i].redirects = redirects;
            return;
        }
    }
    if (ft->func_count >= ft->func_capacity) {
        ft->func_capacity *= 2;
        ft->functions = realloc(ft->functions, ft->func_capacity * sizeof(Function));
    }
    ft->functions[ft->func_count].name = strdup(name);
    ft->functions[ft->func_count].body = body;
    ft->functions[ft->func_count].redirects = redirects;
    ft->func_count++;
}

ASTNode *get_function_body(FunctionTable *ft, const char *name) {
    for (int i = 0; i < ft->func_count; i++) {
        if (strcmp(ft->functions[i].name, name) == 0) return ft->functions[i].body;
    }
    return NULL;
}

Redirect *get_function_redirects(FunctionTable *ft, const char *name) {
    for (int i = 0; i < ft->func_count; i++) {
        if (strcmp(ft->functions[i].name, name) == 0) return ft->functions[i].redirects;
    }
    return NULL;
}

char *get_shebang_interpreter(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return NULL;

    char buffer[MAX_SHEBANG_LEN];
    size_t bytes_read = fread(buffer, 1, MAX_SHEBANG_LEN - 1, file);
    buffer[bytes_read] = '\0';
    fclose(file);

    char *shebang = NULL;
    if (bytes_read >= 2 && strncmp(buffer, "#!", 2) == 0) {
        shebang = buffer + 2;
    } else if (bytes_read >= 5 && strncmp(buffer, "\xEF\xBB\xBF#!", 5) == 0) {
        shebang = buffer + 5;
    } else {
        return NULL;
    }

    char *end = strchr(shebang, '\n');
    if (end) *end = '\0';
    while (*shebang && isspace((unsigned char)*shebang)) shebang++;
    char *interp_end = shebang;
    while (*interp_end && !isspace((unsigned char)*interp_end)) interp_end++;
    if (*interp_end) *interp_end = '\0';

    return strdup(shebang);
}

int has_redirects(Redirect *redirects) {
    return redirects != NULL;
}

static char *get_command_name(ASTNode *node) {
    if (!node) return "unknown";
    if (node->type == AST_SIMPLE_COMMAND && node->data.simple_command.command) {
        return node->data.simple_command.command;
    } else if (node->type == AST_PIPELINE && node->data.pipeline.command_count > 0) {
        return get_command_name(node->data.pipeline.commands[0]);
    } else if (node->type == AST_AND_OR) {
        return get_command_name(node->data.and_or.left);
    }
    return "unknown";
}

ExecStatus execute_ast(ASTNode *node, Environment *env, FunctionTable *ft, int *last_exit_status) {
    if (!node) {
        *last_exit_status = 0;
        return EXEC_NORMAL;
    }

    switch (node->type) {
        case AST_SIMPLE_COMMAND: {
            // Check for redirections, including heredocs
            Redirect *redir = node->data.simple_command.redirects;
            while (redir) {
                if (redir->operator == DLESS || redir->operator == DLESSDASH) {
                    fprintf(stderr, "Error: Heredocs not supported by sh23\n");
                    *last_exit_status = 1;
                    return EXEC_NORMAL;
                }
                if (redir->operator == LESS || redir->operator == GREAT ||
                    redir->operator == DGREAT || redir->operator == LESSAND ||
                    redir->operator == GREATAND || redir->operator == LESSGREAT ||
                    redir->operator == CLOBBER) {
                    fprintf(stderr, "Error: I/O redirection not supported by sh23\n");
                    *last_exit_status = 1;
                    return EXEC_NORMAL;
                }
                redir = redir->next;
            }
            for (int i = 0; i < node->data.simple_command.expansion_count; i++) {
                char *value = expand_parameter(node->data.simple_command.expansions[i], env, ft, last_exit_status);
                if (*last_exit_status != 0) {
                    free(value);
                    return EXEC_NORMAL; // Stop on expansion error
                }
                free(value);
            }
            return execute_simple_command(node, env, ft, last_exit_status);
        }
		case AST_PIPELINE: {
            // Reject pipelines with multiple commands
            if (node->data.pipeline.command_count > 1) {
                char *cmd_name = get_command_name(node->data.pipeline.commands[0]);
                fprintf(stderr, "Error: Pipeline starting with '%s' not supported by sh23\n", cmd_name);
                *last_exit_status = 1;
                return EXEC_NORMAL;
            }
            int bang = node->data.pipeline.bang;
            ExecStatus status = EXEC_NORMAL;
            if (node->data.pipeline.command_count == 1) {
                status = execute_ast(node->data.pipeline.commands[0], env, ft, last_exit_status);
            }
            if (bang && status == EXEC_NORMAL) {
                *last_exit_status = !(*last_exit_status);
            }
            return status;
        }

        case AST_AND_OR: {
            ExecStatus status = execute_ast(node->data.and_or.left, env, ft, last_exit_status);
            if (status != EXEC_NORMAL) {
                return status;
            }
            if ((node->data.and_or.operator == AND_IF && *last_exit_status == 0) ||
                (node->data.and_or.operator == OR_IF && *last_exit_status != 0)) {
                status = execute_ast(node->data.and_or.right, env, ft, last_exit_status);
            }
            return status;
        }

        case AST_LIST: {
            // Reject background execution
            if (node->data.list.separator == AMP) {
                char *cmd_name = get_command_name(node->data.list.and_or);
                fprintf(stderr, "Error: Background execution of '%s' not supported by sh23\n", cmd_name);
                *last_exit_status = 1;
                return EXEC_NORMAL;
            }
            // Recursively check next for background jobs
            if (node->data.list.next) {
                ASTNode *next = node->data.list.next;
                while (next) {
                    if (next->type == AST_LIST && next->data.list.separator == AMP) {
                        char *cmd_name = get_command_name(next->data.list.and_or);
                        fprintf(stderr, "Error: Background execution of '%s' not supported by sh23\n", cmd_name);
                        *last_exit_status = 1;
                        return EXEC_NORMAL;
                    }
                    next = next->data.list.next;
                }
            }
            ExecStatus status = execute_ast(node->data.list.and_or, env, ft, last_exit_status);
            if (status != EXEC_NORMAL) {
                return status;
            }
            if (node->data.list.next) {
                status = execute_ast(node->data.list.next, env, ft, last_exit_status);
            }
            return status;
        }

        case AST_COMPLETE_COMMAND: {
            return execute_ast(node->data.complete_command.list, env, ft, last_exit_status);
        }

        case AST_PROGRAM: {
            return execute_ast(node->data.program.commands, env, ft, last_exit_status);
        }

        case AST_IF_CLAUSE: {
            ExecStatus status = execute_ast(node->data.if_clause.condition, env, ft, last_exit_status);
            if (status != EXEC_NORMAL) {
                return status;
            }
            if (*last_exit_status == 0) {
                status = execute_ast(node->data.if_clause.then_body, env, ft, last_exit_status);
            } else if (node->data.if_clause.else_part) {
                status = execute_ast(node->data.if_clause.else_part, env, ft, last_exit_status);
            }
            return status;
        }

        case AST_FOR_CLAUSE: {
            char wordlist[MAX_COMMAND_LEN];
            wordlist[0] = '\0';
            for (int i = 0; i < node->data.for_clause.wordlist_count; i++) {
                char *expanded = expand_assignment(node->data.for_clause.wordlist[i], env, ft, last_exit_status);
                strncat(wordlist, expanded, MAX_COMMAND_LEN - strlen(wordlist) - 1);
                strncat(wordlist, " ", MAX_COMMAND_LEN - strlen(wordlist) - 1);
                free(expanded);
            }
            char *word = strtok(wordlist, " ");
            while (word) {
                char assignment[MAX_TOKEN_LEN];
                snprintf(assignment, MAX_TOKEN_LEN, "%s=%s", node->data.for_clause.variable, word);
                set_variable(env, assignment);
                ExecStatus status = execute_ast(node->data.for_clause.body, env, ft, last_exit_status);
                if (status != EXEC_NORMAL) {
                    return status;
                }
                word = strtok(NULL, " ");
            }
            *last_exit_status = 0;
            return EXEC_NORMAL;
        }

        case AST_CASE_CLAUSE: {
            char *word = expand_assignment(node->data.case_clause.word, env, ft, last_exit_status);
            CaseItem *item = node->data.case_clause.items;
            int matched = 0;
            while (item && !matched) {
                for (int i = 0; i < item->pattern_count; i++) {
                    char *pattern = item->patterns[i];
                    if (strcmp(word, pattern) == 0) {
                        matched = 1;
                        ExecStatus status = execute_ast(item->action, env, ft, last_exit_status);
                        if (status != EXEC_NORMAL) {
                            free(word);
                            return status;
                        }
                        break;
                    }
                }
                item = item->next;
            }
            free(word);
            *last_exit_status = 0;
            return EXEC_NORMAL;
        }

        case AST_WHILE_CLAUSE: {
            while (1) {
                ExecStatus status = execute_ast(node->data.while_clause.condition, env, ft, last_exit_status);
                if (status != EXEC_NORMAL) {
                    return status;
                }
                if (*last_exit_status != 0) {
                    break;
                }
                status = execute_ast(node->data.while_clause.body, env, ft, last_exit_status);
                if (status != EXEC_NORMAL) {
                    return status;
                }
            }
            *last_exit_status = 0;
            return EXEC_NORMAL;
        }

        case AST_UNTIL_CLAUSE: {
            while (1) {
                ExecStatus status = execute_ast(node->data.until_clause.condition, env, ft, last_exit_status);
                if (status != EXEC_NORMAL) {
                    return status;
                }
                if (*last_exit_status == 0) {
                    break;
                }
                status = execute_ast(node->data.until_clause.body, env, ft, last_exit_status);
                if (status != EXEC_NORMAL) {
                    return status;
                }
            }
            *last_exit_status = 0;
            return EXEC_NORMAL;
        }

        case AST_BRACE_GROUP: {
            return execute_ast(node->data.brace_group.body, env, ft, last_exit_status);
        }

        case AST_SUBSHELL: {
            // Note: No fork, so execute in current environment
            return execute_ast(node->data.subshell.body, env, ft, last_exit_status);
        }

        case AST_FUNCTION_DEFINITION: {
            // Check for redirections, including heredocs
            Redirect *redir = node->data.function_definition.redirects;
            while (redir) {
                if (redir->operator == DLESS || redir->operator == DLESSDASH) {
                    fprintf(stderr, "Error: Heredocs not supported by sh23\n");
                    *last_exit_status = 1;
                    return EXEC_NORMAL;
                }
                if (redir->operator == LESS || redir->operator == GREAT ||
                    redir->operator == DGREAT || redir->operator == LESSAND ||
                    redir->operator == GREATAND || redir->operator == LESSGREAT ||
                    redir->operator == CLOBBER) {
                    fprintf(stderr, "Error: I/O redirection not supported by sh23\n");
                    *last_exit_status = 1;
                    return EXEC_NORMAL;
                }
                redir = redir->next;
            }
            // Store function in FunctionTable
            for (int i = 0; i < ft->func_count; i++) {
                if (strcmp(ft->functions[i].name, node->data.function_definition.name) == 0) {
                    free(ft->functions[i].name);
                    free_ast(ft->functions[i].body);
                    Redirect *r = ft->functions[i].redirects;
                    while (r) {
                        free(r->io_number);
                        free(r->filename);
                        free(r->delimiter);
                        free(r->heredoc_content);
                        Redirect *next = r->next;
                        free(r);
                        r = next;
                    }
                    ft->functions[i] = ft->functions[ft->func_count - 1];
                    ft->func_count--;
                    break;
                }
            }
            if (ft->func_count >= ft->func_capacity) {
                ft->func_capacity = (ft->func_capacity == 0) ? 8 : ft->func_capacity * 2;
                ft->functions = realloc(ft->functions, ft->func_capacity * sizeof(Function));
            }
            ft->functions[ft->func_count].name = strdup(node->data.function_definition.name);
            ft->functions[ft->func_count].body = node->data.function_definition.body;
            ft->functions[ft->func_count].redirects = node->data.function_definition.redirects;
            ft->functions[ft->func_count].active = 0;
            ft->func_count++;
            *last_exit_status = 0;
            return EXEC_NORMAL;
        }

        case AST_IO_REDIRECT: {
            if (node->data.io_redirect.operator == DLESS || node->data.io_redirect.operator == DLESSDASH) {
                fprintf(stderr, "Error: Heredocs not supported by sh23\n");
                *last_exit_status = 1;
                return EXEC_NORMAL;
            }
            fprintf(stderr, "Error: I/O redirection not supported by sh23\n");
            *last_exit_status = 1;
            return EXEC_NORMAL;
        }

        case AST_EXPANSION: {
            char *value = expand_parameter(&node->data.expansion, env, ft, last_exit_status);
            free(value);
            return EXEC_NORMAL;
        }

        default:
            fprintf(stderr, "Error: Unknown AST node type\n");
            *last_exit_status = 1;
            return EXEC_NORMAL;
    }
}

#if 0
void execute_ast(ASTNode *ast, Environment *env, FunctionTable *ft, int *last_exit_status) {
    if (!ast) return;

    switch (ast->type) {
        case AST_PROGRAM:
            execute_ast(ast->data.program.commands, env, ft, last_exit_status);
            break;
        case AST_COMPLETE_COMMAND:
            execute_ast(ast->data.complete_command.list, env, ft, last_exit_status);
            break;
        case AST_LIST:
            execute_ast(ast->data.list.and_or, env, ft, last_exit_status);
            if (ast->data.list.next) {
                if (ast->data.list.separator == AMP) {
                    fprintf(stderr, "Error: Background execution (&) not supported in C23\n");
                    *last_exit_status = 1;
                } else {
                    execute_ast(ast->data.list.next, env, ft, last_exit_status);
                }
            }
            break;
        case AST_AND_OR:
            execute_ast(ast->data.and_or.left, env, ft, last_exit_status);
            if (ast->data.and_or.operator == AND_IF && *last_exit_status == 0) {
                execute_ast(ast->data.and_or.right, env, ft, last_exit_status);
            } else if (ast->data.and_or.operator == OR_IF && *last_exit_status != 0) {
                execute_ast(ast->data.and_or.right, env, ft, last_exit_status);
            }
            break;
        case AST_PIPELINE:
            if (ast->data.pipeline.command_count > 1) {
                fprintf(stderr, "Error: Pipelines (|) not supported in C23\n");
                *last_exit_status = 1;
                break;
            }
            execute_ast(ast->data.pipeline.commands[0], env, ft, last_exit_status);
            if (ast->data.pipeline.bang) *last_exit_status = !*last_exit_status;
            break;
        case AST_SIMPLE_COMMAND:
            execute_simple_command(ast, env, ft, last_exit_status);
            break;
        case AST_FUNCTION_DEFINITION:
            if (has_redirects(ast->data.function_definition.redirects)) {
                fprintf(stderr, "Error: Function %s definition uses unsupported I/O redirection\n", ast->data.function_definition.name);
                *last_exit_status = 1;
            } else {
                add_function(ft, ast->data.function_definition.name, ast->data.function_definition.body, ast->data.function_definition.redirects);
                ast->data.function_definition.body = NULL;
                ast->data.function_definition.redirects = NULL;
            }
            break;
        case AST_IF_CLAUSE:
            execute_ast(ast->data.if_clause.condition, env, ft, last_exit_status);
            if (*last_exit_status == 0) {
                execute_ast(ast->data.if_clause.then_body, env, ft, last_exit_status);
            } else if (ast->data.if_clause.else_part) {
                execute_ast(ast->data.if_clause.else_part, env, ft, last_exit_status);
            }
            break;
        case AST_FOR_CLAUSE:
            for (int i = 0; i < ast->data.for_clause.wordlist_count; i++) {
                char *assignment = malloc(strlen(ast->data.for_clause.variable) + strlen(ast->data.for_clause.wordlist[i]) + 2);
                sprintf(assignment, "%s=%s", ast->data.for_clause.variable, ast->data.for_clause.wordlist[i]);
                set_variable(env, assignment);
                free(assignment);
                execute_ast(ast->data.for_clause.body, env, ft, last_exit_status);
            }
            break;
        case AST_CASE_CLAUSE:
            {
                char *word = expand_assignment(ast->data.case_clause.word, env, ft, last_exit_status);
                char *equals = strchr(word, '=');
                if (equals) *equals = '\0';
                CaseItem *item = ast->data.case_clause.items;
                while (item) {
                    for (int i = 0; i < item->pattern_count; i++) {
                        if (strcmp(equals ? equals + 1 : word, item->patterns[i]) == 0) {
                            if (item->action) execute_ast(item->action, env, ft, last_exit_status);
                            free(word);
                            return;
                        }
                    }
                    item = item->next;
                }
                free(word);
            }
            break;
        case AST_WHILE_CLAUSE:
            while (1) {
                execute_ast(ast->data.while_clause.condition, env, ft, last_exit_status);
                if (*last_exit_status != 0) break;
                execute_ast(ast->data.while_clause.body, env, ft, last_exit_status);
            }
            break;
        case AST_UNTIL_CLAUSE:
            while (1) {
                execute_ast(ast->data.until_clause.condition, env, ft, last_exit_status);
                if (*last_exit_status == 0) break;
                execute_ast(ast->data.until_clause.body, env, ft, last_exit_status);
            }
            break;
        case AST_BRACE_GROUP:
            execute_ast(ast->data.brace_group.body, env, ft, last_exit_status);
            break;
        case AST_SUBSHELL:
            execute_ast(ast->data.subshell.body, env, ft, last_exit_status);
            break;
        case AST_IO_REDIRECT:
            fprintf(stderr, "Error: Standalone I/O redirection not supported in C23\n");
            *last_exit_status = 1;
            break;
    }
}
#endif


void free_ast(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_SIMPLE_COMMAND:
            for (int i = 0; i < node->data.simple_command.prefix_count; i++) {
                free(node->data.simple_command.prefix[i]);
            }
            free(node->data.simple_command.prefix);
            if (node->data.simple_command.command) {
                free(node->data.simple_command.command);
            }
            for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
                free(node->data.simple_command.suffix[i]);
            }
            for (int i = 0; i < node->data.simple_command.expansion_count; i++) {
                Expansion *exp = node->data.simple_command.expansions[i];
                switch (exp->type) {
                    case EXPANSION_PARAMETER:
                        free(exp->data.parameter.name);
                        break;
                    case EXPANSION_SPECIAL:
                        free(exp->data.special.name);
                        break;
                    case EXPANSION_DEFAULT:
                    case EXPANSION_ASSIGN:
                        free(exp->data.default_exp.var);
                        free(exp->data.default_exp.default_value);
                        break;
                    case EXPANSION_SUBSTRING:
                        free(exp->data.substring.var);
                        free(exp->data.substring.offset);
                        if (exp->data.substring.length) free(exp->data.substring.length);
                        break;
                    case EXPANSION_LENGTH:
                        free(exp->data.length.var);
                        break;
                    case EXPANSION_PREFIX_SHORT:
                    case EXPANSION_PREFIX_LONG:
                    case EXPANSION_SUFFIX_SHORT:
                    case EXPANSION_SUFFIX_LONG:
                        free(exp->data.pattern.var);
                        free(exp->data.pattern.pattern);
                        break;
                    case EXPANSION_COMMAND:
                        free_ast(exp->data.command.command);
                        break;
                    case EXPANSION_ARITHMETIC:
                        free(exp->data.arithmetic.expression);
                        break;
                    case EXPANSION_TILDE:
                        if (exp->data.tilde.user) free(exp->data.tilde.user);
                        break;
                    default:
                        break;
                }
                free(exp);
            }
            free(node->data.simple_command.expansions);
            {
                Redirect *redir = node->data.simple_command.redirects;
                while (redir) {
                    free(redir->io_number);
                    free(redir->filename);
                    free(redir->delimiter);
                    free(redir->heredoc_content);
                    Redirect *next = redir->next;
                    free(redir);
                    redir = next;
                }
            }
            break;

        case AST_EXPANSION: {
            Expansion *exp = &node->data.expansion;
            switch (exp->type) {
                case EXPANSION_PARAMETER:
                    free(exp->data.parameter.name);
                    break;
                case EXPANSION_SPECIAL:
                    free(exp->data.special.name);
                    break;
                case EXPANSION_DEFAULT:
                case EXPANSION_ASSIGN:
                    free(exp->data.default_exp.var);
                    free(exp->data.default_exp.default_value);
                    break;
                case EXPANSION_SUBSTRING:
                    free(exp->data.substring.var);
                    free(exp->data.substring.offset);
                    if (exp->data.substring.length) free(exp->data.substring.length);
                    break;
                case EXPANSION_LENGTH:
                    free(exp->data.length.var);
                    break;
                case EXPANSION_PREFIX_SHORT:
                case EXPANSION_PREFIX_LONG:
                case EXPANSION_SUFFIX_SHORT:
                case EXPANSION_SUFFIX_LONG:
                    free(exp->data.pattern.var);
                    free(exp->data.pattern.pattern);
                    break;
                case EXPANSION_COMMAND:
                    free_ast(exp->data.command.command);
                    break;
                case EXPANSION_ARITHMETIC:
                    free(exp->data.arithmetic.expression);
                    break;
                case EXPANSION_TILDE:
                    if (exp->data.tilde.user) free(exp->data.tilde.user);
                    break;
                default:
                    break;
            }
            break;
        }
        case AST_PIPELINE:
            for (int i = 0; i < node->data.pipeline.command_count; i++) {
                free_ast(node->data.pipeline.commands[i]);
            }
            free(node->data.pipeline.commands);
            break;

        case AST_AND_OR:
            free_ast(node->data.and_or.left);
            free_ast(node->data.and_or.right);
            break;

        case AST_LIST:
            free_ast(node->data.list.and_or);
            free_ast(node->data.list.next);
            break;

        case AST_COMPLETE_COMMAND:
            free_ast(node->data.complete_command.list);
            break;

        case AST_PROGRAM:
            free_ast(node->data.program.commands);
            break;

        case AST_IF_CLAUSE:
            free_ast(node->data.if_clause.condition);
            free_ast(node->data.if_clause.then_body);
            free_ast(node->data.if_clause.else_part);
            break;

        case AST_FOR_CLAUSE:
            free(node->data.for_clause.variable);
            for (int i = 0; i < node->data.for_clause.wordlist_count; i++) {
                free(node->data.for_clause.wordlist[i]);
            }
            free(node->data.for_clause.wordlist);
            free_ast(node->data.for_clause.body);
            break;

        case AST_CASE_CLAUSE:
            free(node->data.case_clause.word);
            {
                CaseItem *item = node->data.case_clause.items;
                while (item) {
                    for (int i = 0; i < item->pattern_count; i++) {
                        free(item->patterns[i]);
                    }
                    free(item->patterns);
                    free_ast(item->action);
                    CaseItem *next = item->next;
                    free(item);
                    item = next;
                }
            }
            break;

        case AST_WHILE_CLAUSE:
            free_ast(node->data.while_clause.condition);
            free_ast(node->data.while_clause.body);
            break;

        case AST_UNTIL_CLAUSE:
            free_ast(node->data.until_clause.condition);
            free_ast(node->data.until_clause.body);
            break;

        case AST_BRACE_GROUP:
            free_ast(node->data.brace_group.body);
            break;

        case AST_SUBSHELL:
            free_ast(node->data.subshell.body);
            break;

        case AST_FUNCTION_DEFINITION:
            free(node->data.function_definition.name);
            free_ast(node->data.function_definition.body);
            {
                Redirect *redir = node->data.function_definition.redirects;
                while (redir) {
                    free(redir->io_number);
                    free(redir->filename);
                    free(redir->delimiter);
                    free(redir->heredoc_content);
                    Redirect *next = redir->next;
                    free(redir);
                    redir = next;
                }
            }
            break;

        case AST_IO_REDIRECT:
            free(node->data.io_redirect.io_number);
            free(node->data.io_redirect.filename);
            free(node->data.io_redirect.delimiter);
            free(node->data.io_redirect.heredoc_content);
            break;

        default:
            // Unknown node types are freed without specific handling
            break;
    }

    free(node);
}

void print_ast(ASTNode *node, int depth) {
    if (!node) {
        for (int i = 0; i < depth; i++) printf("  ");
        printf("NULL\n");
        return;
    }

    for (int i = 0; i < depth; i++) printf("  ");
    switch (node->type) {
        case AST_SIMPLE_COMMAND:
            printf("AST_SIMPLE_COMMAND\n");
            if (node->data.simple_command.prefix_count > 0) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("prefix:\n");
                for (int i = 0; i < node->data.simple_command.prefix_count; i++) {
                    for (int j = 0; j < depth + 2; j++) printf("  ");
                    printf("%s\n", node->data.simple_command.prefix[i]);
                }
            }
            if (node->data.simple_command.command) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("command: %s\n", node->data.simple_command.command);
            }
            if (node->data.simple_command.suffix_count > 0) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("suffix:\n");
                for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
                    for (int j = 0; j < depth + 2; j++) printf("  ");
                    printf("%s\n", node->data.simple_command.suffix[i]);
                }
            }
            if (node->data.simple_command.expansion_count > 0) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("expansions:\n");
                for (int i = 0; i < node->data.simple_command.expansion_count; i++) {
                    for (int j = 0; j < depth + 2; j++) printf("  ");
                    Expansion *exp = node->data.simple_command.expansions[i];
                    switch (exp->type) {
                        case EXPANSION_PARAMETER:
                            printf("PARAMETER: %s\n", exp->data.parameter.name);
                            break;
                        case EXPANSION_SPECIAL:
                            printf("SPECIAL: %s\n", exp->data.special.name);
                            break;
                        case EXPANSION_DEFAULT:
                            printf("DEFAULT: %s :- %s (colon: %d)\n",
                                   exp->data.default_exp.var, exp->data.default_exp.default_value,
                                   exp->data.default_exp.is_colon);
                            break;
                        case EXPANSION_ASSIGN:
                            printf("ASSIGN: %s := %s (colon: %d)\n",
                                   exp->data.default_exp.var, exp->data.default_exp.default_value,
                                   exp->data.default_exp.is_colon);
                            break;
                        case EXPANSION_SUBSTRING:
                            printf("SUBSTRING: %s : %s", exp->data.substring.var, exp->data.substring.offset);
                            if (exp->data.substring.length) printf(" : %s", exp->data.substring.length);
                            printf("\n");
                            break;
                        case EXPANSION_LENGTH:
                            printf("LENGTH: %s\n", exp->data.length.var);
                            break;
                        case EXPANSION_PREFIX_SHORT:
                            printf("PREFIX_SHORT: %s # %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                            break;
                        case EXPANSION_PREFIX_LONG:
                            printf("PREFIX_LONG: %s ## %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                            break;
                        case EXPANSION_SUFFIX_SHORT:
                            printf("SUFFIX_SHORT: %s %% %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                            break;
                        case EXPANSION_SUFFIX_LONG:
                            printf("SUFFIX_LONG: %s %%%% %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                            break;
                        case EXPANSION_COMMAND:
                            printf("COMMAND: %s\n", exp->data.command.command ? "" : "empty");
                            if (exp->data.command.command) {
                                print_ast(exp->data.command.command, depth + 3);
                            }
                            break;
                        case EXPANSION_ARITHMETIC:
                            printf("ARITHMETIC: %s\n", exp->data.arithmetic.expression);
                            break;
                        case EXPANSION_TILDE:
                            printf("TILDE: %s\n", exp->data.tilde.user ? exp->data.tilde.user : "");
                            break;
                        default:
                            printf("UNKNOWN_EXPANSION\n");
                    }
                }
            }
            if (node->data.simple_command.redirects) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("redirects:\n");
                Redirect *redir = node->data.simple_command.redirects;
                while (redir) {
                    for (int j = 0; j < depth + 2; j++) printf("  ");
                    const char *op_str = "UNKNOWN";
                    switch (redir->operator) {
                        case LESS: op_str = "<"; break;
                        case GREAT: op_str = ">"; break;
                        case DGREAT: op_str = ">>"; break;
                        case DLESS: op_str = "<<"; break;
                        case DLESSDASH: op_str = "<<-"; break;
                        case LESSAND: op_str = "<&"; break;
                        case GREATAND: op_str = ">&"; break;
                        case LESSGREAT: op_str = "<>"; break;
                        case CLOBBER: op_str = ">|"; break;
                        default: break;
                    }
                    printf("operator: %s", op_str);
                    if (redir->io_number) printf(", io_number: %s", redir->io_number);
                    if (redir->filename) printf(", filename: %s", redir->filename);
                    if (redir->delimiter) printf(", delimiter: %s", redir->delimiter);
                    if (redir->heredoc_content) {
                        printf(", heredoc_content: \"");
                        for (char *c = redir->heredoc_content; *c; c++) {
                            if (*c == '\n') printf("\\n");
                            else printf("%c", *c);
                        }
                        printf("\"");
                    }
                    printf(", is_quoted: %d, is_dash: %d\n", redir->is_quoted, redir->is_dash);
                    redir = redir->next;
                }
            }
            break;
        case AST_EXPANSION: {
            Expansion *exp = &node->data.expansion;
            switch (exp->type) {
                case EXPANSION_COMMAND:
                    printf("AST_EXPANSION COMMAND: %s\n", exp->data.command.command ? "" : "empty");
                    if (exp->data.command.command) {
                        print_ast(exp->data.command.command, depth + 1);
                    }
                    break;
                case EXPANSION_PARAMETER:
                    printf("AST_EXPANSION PARAMETER: %s\n", exp->data.parameter.name);
                    break;
                case EXPANSION_SPECIAL:
                    printf("AST_EXPANSION SPECIAL: %s\n", exp->data.special.name);
                    break;
                case EXPANSION_DEFAULT:
                    printf("AST_EXPANSION DEFAULT: %s :- %s (colon: %d)\n",
                           exp->data.default_exp.var, exp->data.default_exp.default_value,
                           exp->data.default_exp.is_colon);
                    break;
                case EXPANSION_ASSIGN:
                    printf("AST_EXPANSION ASSIGN: %s := %s (colon: %d)\n",
                           exp->data.default_exp.var, exp->data.default_exp.default_value,
                           exp->data.default_exp.is_colon);
                    break;
                case EXPANSION_SUBSTRING:
                    printf("AST_EXPANSION SUBSTRING: %s : %s", exp->data.substring.var, exp->data.substring.offset);
                    if (exp->data.substring.length) printf(" : %s", exp->data.substring.length);
                    printf("\n");
                    break;
                case EXPANSION_LENGTH:
                    printf("AST_EXPANSION LENGTH: %s\n", exp->data.length.var);
                    break;
                case EXPANSION_PREFIX_SHORT:
                    printf("AST_EXPANSION PREFIX_SHORT: %s # %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                    break;
                case EXPANSION_PREFIX_LONG:
                    printf("AST_EXPANSION PREFIX_LONG: %s ## %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                    break;
                case EXPANSION_SUFFIX_SHORT:
                    printf("AST_EXPANSION SUFFIX_SHORT: %s %% %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                    break;
                case EXPANSION_SUFFIX_LONG:
                    printf("AST_EXPANSION SUFFIX_LONG: %s %%%% %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                    break;
                case EXPANSION_COMMAND:
                    printf("AST_EXPANSION COMMAND:\n");
                    print_ast(exp->data.command.command, depth + 1);
                    break;
                case EXPANSION_ARITHMETIC:
                    printf("AST_EXPANSION ARITHMETIC: %s\n", exp->data.arithmetic.expression);
                    break;
                case EXPANSION_TILDE:
                    printf("AST_EXPANSION TILDE: %s\n", exp->data.tilde.user ? exp->data.tilde.user : "");
                    break;
                default:
                    printf("AST_EXPANSION UNKNOWN\n");
            }
            break;
        }
		
        case AST_PIPELINE:
            printf("AST_PIPELINE (bang: %d)\n", node->data.pipeline.bang);
            for (int i = 0; i < node->data.pipeline.command_count; i++) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("command[%d]:\n", i);
                print_ast(node->data.pipeline.commands[i], depth + 2);
            }
            break;

        case AST_AND_OR:
            printf("AST_AND_OR (operator: %s)\n",
                   node->data.and_or.operator == AND_IF ? "&&" : "||");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("left:\n");
            print_ast(node->data.and_or.left, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("right:\n");
            print_ast(node->data.and_or.right, depth + 2);
            break;

        case AST_LIST:
            printf("AST_LIST (separator: %s)\n",
                   node->data.list.separator == SEMI ? ";" :
                   node->data.list.separator == AMP ? "&" : "none");
            if (node->data.list.and_or) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("and_or:\n");
                print_ast(node->data.list.and_or, depth + 2);
            } else {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("and_or: empty\n");
            }
            if (node->data.list.next) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("next:\n");
                print_ast(node->data.list.next, depth + 2);
            }
            break;

        case AST_COMPLETE_COMMAND:
            printf("AST_COMPLETE_COMMAND\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("list:\n");
            print_ast(node->data.complete_command.list, depth + 2);
            break;

        case AST_PROGRAM:
            printf("AST_PROGRAM\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("commands:\n");
            print_ast(node->data.program.commands, depth + 2);
            break;

        case AST_IF_CLAUSE:
            printf("AST_IF_CLAUSE\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("condition:\n");
            print_ast(node->data.if_clause.condition, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("then_body:\n");
            print_ast(node->data.if_clause.then_body, depth + 2);
            if (node->data.if_clause.else_part) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("else_part:\n");
                print_ast(node->data.if_clause.else_part, depth + 2);
            }
            break;

        case AST_FOR_CLAUSE:
            printf("AST_FOR_CLAUSE (variable: %s)\n", node->data.for_clause.variable);
            if (node->data.for_clause.wordlist_count > 0) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("wordlist:\n");
                for (int i = 0; i < node->data.for_clause.wordlist_count; i++) {
                    for (int j = 0; j < depth + 2; j++) printf("  ");
                    printf("%s\n", node->data.for_clause.wordlist[i]);
                }
            }
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("body:\n");
            print_ast(node->data.for_clause.body, depth + 2);
            break;

        case AST_CASE_CLAUSE:
            printf("AST_CASE_CLAUSE (word: %s)\n", node->data.case_clause.word);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("items:\n");
            {
                CaseItem *item = node->data.case_clause.items;
                while (item) {
                    for (int j = 0; j < depth + 2; j++) printf("  ");
                    printf("case_item (has_dsemi: %d):\n", item->has_dsemi);
                    for (int j = 0; j < depth + 3; j++) printf("  ");
                    printf("patterns:\n");
                    for (int k = 0; k < item->pattern_count; k++) {
                        for (int j = 0; j < depth + 4; j++) printf("  ");
                        printf("%s\n", item->patterns[k]);
                    }
                    for (int j = 0; j < depth + 3; j++) printf("  ");
                    printf("action:\n");
                    print_ast(item->action, depth + 4);
                    item = item->next;
                }
            }
            break;

        case AST_WHILE_CLAUSE:
            printf("AST_WHILE_CLAUSE\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("condition:\n");
            print_ast(node->data.while_clause.condition, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("body:\n");
            print_ast(node->data.while_clause.body, depth + 2);
            break;

        case AST_UNTIL_CLAUSE:
            printf("AST_UNTIL_CLAUSE\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("condition:\n");
            print_ast(node->data.until_clause.condition, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("body:\n");
            print_ast(node->data.until_clause.body, depth + 2);
            break;

        case AST_BRACE_GROUP:
            printf("AST_BRACE_GROUP\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("body:\n");
            print_ast(node->data.brace_group.body, depth + 2);
            break;

        case AST_SUBSHELL:
            printf("AST_SUBSHELL\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("body:\n");
            print_ast(node->data.subshell.body, depth + 2);
            break;

        case AST_FUNCTION_DEFINITION:
            printf("AST_FUNCTION_DEFINITION (name: %s)\n", node->data.function_definition.name);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("body:\n");
            print_ast(node->data.function_definition.body, depth + 2);
            if (node->data.function_definition.redirects) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("redirects:\n");
                Redirect *redir = node->data.function_definition.redirects;
                while (redir) {
                    for (int j = 0; j < depth + 2; j++) printf("  ");
                    const char *op_str = "UNKNOWN";
                    switch (redir->operator) {
                        case LESS: op_str = "<"; break;
                        case GREAT: op_str = ">"; break;
                        case DGREAT: op_str = ">>"; break;
                        case DLESS: op_str = "<<"; break;
                        case DLESSDASH: op_str = "<<-"; break;
                        case LESSAND: op_str = "<&"; break;
                        case GREATAND: op_str = ">&"; break;
                        case LESSGREAT: op_str = "<>"; break;
                        case CLOBBER: op_str = ">|"; break;
                        default: break;
                    }
                    printf("operator: %s", op_str);
                    if (redir->io_number) printf(", io_number: %s", redir->io_number);
                    if (redir->filename) printf(", filename: %s", redir->filename);
                    if (redir->delimiter) printf(", delimiter: %s", redir->delimiter);
                    if (redir->heredoc_content) {
                        printf(", heredoc_content: \"");
                        for (char *c = redir->heredoc_content; *c; c++) {
                            if (*c == '\n') printf("\\n");
                            else printf("%c", *c);
                        }
                        printf("\"");
                    }
                    printf(", is_quoted: %d, is_dash: %d\n", redir->is_quoted, redir->is_dash);
                    redir = redir->next;
                }
            }
            break;

        case AST_IO_REDIRECT:
            printf("AST_IO_REDIRECT (operator: ");
            switch (node->data.io_redirect.operator) {
                case LESS: printf("<"); break;
                case GREAT: printf(">"); break;
                case DGREAT: printf(">>"); break;
                case DLESS: printf("<<"); break;
                case DLESSDASH: printf("<<-"); break;
                case LESSAND: printf("<&"); break;
                case GREATAND: printf(">&"); break;
                case LESSGREAT: printf("<>"); break;
                case CLOBBER: printf(">|"); break;
                default: printf("UNKNOWN");
            }
            printf(")");
            if (node->data.io_redirect.io_number) printf(", io_number: %s", node->data.io_redirect.io_number);
            if (node->data.io_redirect.filename) printf(", filename: %s", node->data.io_redirect.filename);
            if (node->data.io_redirect.delimiter) printf(", delimiter: %s", node->data.io_redirect.delimiter);
            if (node->data.io_redirect.heredoc_content) {
                printf(", heredoc_content: \"");
                for (char *c = node->data.io_redirect.heredoc_content; *c; c++) {
                    if (*c == '\n') printf("\\n");
                    else printf("%c", *c);
                }
                printf("\"");
            }
            printf(", is_quoted: %d, is_dash: %d\n", node->data.io_redirect.is_quoted, node->data.io_redirect.is_dash);
            break;

        case AST_EXPANSION: {
            Expansion *exp = &node->data.expansion;
            switch (exp->type) {
                case EXPANSION_PARAMETER:
                    printf("AST_EXPANSION PARAMETER: %s\n", exp->data.parameter.name);
                    break;
                case EXPANSION_SPECIAL:
                    printf("AST_EXPANSION SPECIAL: %s\n", exp->data.special.name);
                    break;
                case EXPANSION_DEFAULT:
                    printf("AST_EXPANSION DEFAULT: %s :- %s (colon: %d)\n",
                           exp->data.default_exp.var, exp->data.default_exp.default_value,
                           exp->data.default_exp.is_colon);
                    break;
                case EXPANSION_ASSIGN:
                    printf("AST_EXPANSION ASSIGN: %s := %s (colon: %d)\n",
                           exp->data.default_exp.var, exp->data.default_exp.default_value,
                           exp->data.default_exp.is_colon);
                    break;
                case EXPANSION_SUBSTRING:
                    printf("AST_EXPANSION SUBSTRING: %s : %s", exp->data.substring.var, exp->data.substring.offset);
                    if (exp->data.substring.length) printf(" : %s", exp->data.substring.length);
                    printf("\n");
                    break;
                case EXPANSION_LENGTH:
                    printf("AST_EXPANSION LENGTH: %s\n", exp->data.length.var);
                    break;
                case EXPANSION_PREFIX_SHORT:
                    printf("AST_EXPANSION PREFIX_SHORT: %s # %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                    break;
                case EXPANSION_PREFIX_LONG:
                    printf("AST_EXPANSION PREFIX_LONG: %s ## %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                    break;
                case EXPANSION_SUFFIX_SHORT:
                    printf("AST_EXPANSION SUFFIX_SHORT: %s %% %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                    break;
                case EXPANSION_SUFFIX_LONG:
                    printf("AST_EXPANSION SUFFIX_LONG: %s %%%% %s\n", exp->data.pattern.var, exp->data.pattern.pattern);
                    break;
                case EXPANSION_COMMAND:
                    printf("AST_EXPANSION COMMAND:\n");
                    print_ast(exp->data.command.command, depth + 1);
                    break;
                case EXPANSION_ARITHMETIC:
                    printf("AST_EXPANSION ARITHMETIC: %s\n", exp->data.arithmetic.expression);
                    break;
                case EXPANSION_TILDE:
                    printf("AST_EXPANSION TILDE: %s\n", exp->data.tilde.user ? exp->data.tilde.user : "");
                    break;
                default:
                    printf("AST_EXPANSION UNKNOWN\n");
            }
            break;
        }

        default:
            printf("UNKNOWN_NODE\n");
            break;
    }
}

// Forward declaration for recursive evaluation
static int evaluate_expression(int argc, char **argv, int start, int end, int *last_exit_status);

// Helper to evaluate a single test (no logical operators)
static int evaluate_single_test(int argc, char **argv, int *last_exit_status) {
    if (argc == 1) {
        return strlen(argv[0]) == 0 ? 1 : 0; // True if non-empty string
    }
    if (argc == 2) {
        if (strcmp(argv[0], "-n") == 0) return strlen(argv[1]) > 0 ? 0 : 1;
        if (strcmp(argv[0], "-z") == 0) return strlen(argv[1]) == 0 ? 0 : 1;
        if (strcmp(argv[0], "-e") == 0) {
            FILE *f = fopen(argv[1], "r");
            if (f) {
                fclose(f);
                return 0;
            }
            return 1;
        }
        if (strcmp(argv[0], "-r") == 0) {
            FILE *f = fopen(argv[1], "r");
            if (f) {
                fclose(f);
                return 0;
            }
            return 1;
        }
        if (strcmp(argv[0], "-w") == 0) {
            FILE *f = fopen(argv[1], "r+");
            if (f) {
                fclose(f);
                return 0;
            }
            f = fopen(argv[1], "w");
            if (f) {
                fclose(f);
                remove(argv[1]);
                return 0;
            }
            return 1;
        }
        if (argv[0][0] == '-' && strchr("bcdfghkLpsSux", argv[0][1])) {
            fprintf(stderr, "test: %s: file type tests not supported in C23\n", argv[0]);
            return 2;
        }
        fprintf(stderr, "test: %s: unknown unary operator\n", argv[0]);
        return 2;
    }
    if (argc == 3) {
        if (strcmp(argv[1], "=") == 0) return strcmp(argv[0], argv[2]) == 0 ? 0 : 1;
        if (strcmp(argv[1], "!=") == 0) return strcmp(argv[0], argv[2]) != 0 ? 0 : 1;

        char *endptr1, *endptr2;
        long n1 = strtol(argv[0], &endptr1, 10);
        long n2 = strtol(argv[2], &endptr2, 10);
        if (*endptr1 == '\0' && *endptr2 == '\0') {
            if (strcmp(argv[1], "-eq") == 0) return n1 == n2 ? 0 : 1;
            if (strcmp(argv[1], "-ne") == 0) return n1 != n2 ? 0 : 1;
            if (strcmp(argv[1], "-lt") == 0) return n1 < n2 ? 0 : 1;
            if (strcmp(argv[1], "-gt") == 0) return n1 > n2 ? 0 : 1;
            if (strcmp(argv[1], "-le") == 0) return n1 <= n2 ? 0 : 1;
            if (strcmp(argv[1], "-ge") == 0) return n1 >= n2 ? 0 : 1;
        }
        fprintf(stderr, "test: %s: unknown binary operator or invalid operands\n", argv[1]);
        return 2;
    }
    fprintf(stderr, "test: invalid single test expression\n");
    return 2;
}

// Evaluate expression with logical operators
static int evaluate_expression(int argc, char **argv, int start, int end, int *last_exit_status) {
    if (start >= end) return 1; // Empty expression is false

    int paren_depth = 0;
    int or_pos = -1;
    int and_pos = -1;

    // Scan for operators outside parentheses
    for (int i = start; i < end; i++) {
        if (strcmp(argv[i], "(") == 0) paren_depth++;
        else if (strcmp(argv[i], ")") == 0) paren_depth--;
        else if (paren_depth == 0) {
            if (strcmp(argv[i], "-o") == 0 && or_pos == -1) or_pos = i;
            else if (strcmp(argv[i], "-a") == 0) and_pos = i;
        }
    }
    if (paren_depth != 0) {
        fprintf(stderr, "test: unmatched parentheses\n");
        return 2;
    }

    // Handle ! (highest precedence)
    if (start < end && strcmp(argv[start], "!") == 0) {
        int sub_result = evaluate_expression(argc, argv, start + 1, end, last_exit_status);
        return sub_result == 0 ? 1 : (sub_result == 1 ? 0 : 2);
    }

    // Handle parentheses
    if (start < end && strcmp(argv[start], "(") == 0) {
        int paren_end = start;
        int depth = 1;
        while (paren_end + 1 < end && depth > 0) {
            paren_end++;
            if (strcmp(argv[paren_end], "(") == 0) depth++;
            else if (strcmp(argv[paren_end], ")") == 0) depth--;
        }
        if (depth != 0 || paren_end >= end) {
            fprintf(stderr, "test: unmatched parentheses\n");
            return 2;
        }
        int sub_result = evaluate_expression(argc, argv, start + 1, paren_end, last_exit_status);
        if (sub_result == 2) return 2;
        if (paren_end + 1 == end) return sub_result;
        if (strcmp(argv[paren_end + 1], "-a") == 0) {
            int right_result = evaluate_expression(argc, argv, paren_end + 2, end, last_exit_status);
            return (sub_result == 0 && right_result == 0) ? 0 : (sub_result == 2 || right_result == 2 ? 2 : 1);
        }
        if (strcmp(argv[paren_end + 1], "-o") == 0) {
            int right_result = evaluate_expression(argc, argv, paren_end + 2, end, last_exit_status);
            return (sub_result == 0 || right_result == 0) ? 0 : (sub_result == 2 || right_result == 2 ? 2 : 1);
        }
        fprintf(stderr, "test: invalid expression after parentheses\n");
        return 2;
    }

    // Handle -o (lowest precedence)
    if (or_pos != -1) {
        int left_result = evaluate_expression(argc, argv, start, or_pos, last_exit_status);
        if (left_result == 0) return 0; // Short-circuit OR
        if (left_result == 2) return 2;
        int right_result = evaluate_expression(argc, argv, or_pos + 1, end, last_exit_status);
        return (left_result == 0 || right_result == 0) ? 0 : (right_result == 2 ? 2 : 1);
    }

    // Handle -a
    if (and_pos != -1) {
        int left_result = evaluate_expression(argc, argv, start, and_pos, last_exit_status);
        if (left_result == 1) return 1; // Short-circuit AND
        if (left_result == 2) return 2;
        int right_result = evaluate_expression(argc, argv, and_pos + 1, end, last_exit_status);
        return (left_result == 0 && right_result == 0) ? 0 : (right_result == 2 ? 2 : 1);
    }

    // No logical operators, evaluate as single test
    char *sub_args[end - start + 1];
    for (int i = start; i < end; i++) sub_args[i - start] = argv[i];
    return evaluate_single_test(end - start, sub_args, last_exit_status);
}

static int evaluate_test(int argc, char **argv, int *last_exit_status) {
    if (argc == 0) return 1; // No args

    int expect_bracket = (strcmp(argv[0], "[") == 0);
    if (expect_bracket && (argc < 2 || strcmp(argv[argc - 1], "]") != 0)) {
        fprintf(stderr, "%s: missing ']'\n", argv[0]);
        return 2;
    }
    int start = 1;
    int end = expect_bracket ? argc - 1 : argc;
    return evaluate_expression(argc, argv, start, end, last_exit_status);
}

// Helper function to get PATH or default
static const char *get_path(Environment *env) {
    const char *path = get_variable(env, "PATH");
    return path ? path : "/bin:/usr/bin"; // Default PATH if unset
}

// Helper function to search PATH for a file
static char *find_file_in_path(const char *filename, const char *path) {
    if (strchr(filename, '/')) { // Absolute or relative path
        FILE *file = fopen(filename, "r");
        if (file) {
            fclose(file);
            return strdup(filename);
        }
        return NULL;
    }

    char *path_copy = strdup(path);
    char *token = strtok(path_copy, ":");
    char full_path[MAX_COMMAND_LEN];

    while (token) {
        snprintf(full_path, sizeof(full_path), "%s/%s", token, filename);
        FILE *file = fopen(full_path, "r");
        if (file) {
            fclose(file);
            free(path_copy);
            return strdup(full_path);
        }
        token = strtok(NULL, ":");
    }
    free(path_copy);
    return NULL;
}

ExecStatus execute_simple_command(ASTNode *node, Environment *env, FunctionTable *ft, int *last_exit_status) {
    for (int i = 0; i < node->data.simple_command.prefix_count; i++) {
        set_variable(env, node->data.simple_command.prefix[i]);
    }
    if (!node->data.simple_command.command) return EXEC_NORMAL;

    if (strcmp(node->data.simple_command.command, "%showvars") == 0) {
        show_variables(env);
        *last_exit_status = 0;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, "export") == 0) {
        if (node->data.simple_command.suffix_count == 0) {
            show_variables(env);
        } else {
            for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
                char *arg = expand_assignment(node->data.simple_command.suffix[i], env, ft, last_exit_status);
                char *equals = strchr(arg, '=');
                if (equals) {
                    *equals = '\0';
                    set_variable(env, node->data.simple_command.suffix[i]);
                    export_variable(env, arg);
                } else {
                    export_variable(env, arg);
                }
                free(arg);
            }
        }
        *last_exit_status = 0;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, "unset") == 0) {
        if (node->data.simple_command.suffix_count == 0) {
            fprintf(stderr, "unset: missing argument\n");
            *last_exit_status = 1;
            return EXEC_NORMAL;
        }
        for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
            char *arg = expand_assignment(node->data.simple_command.suffix[i], env, ft, last_exit_status);
            char *equals = strchr(arg, '=');
            if (equals) *equals = '\0';
            unset_variable(env, arg);
            free(arg);
        }
        *last_exit_status = 0;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, "echo") == 0) {
        int newline = 1;
        int start_idx = 0;

        if (node->data.simple_command.suffix_count > 0) {
            char *first_arg = expand_assignment(node->data.simple_command.suffix[0], env, ft, last_exit_status);
            char *equals = strchr(first_arg, '=');
            if (equals) *equals = '\0';
            if (strcmp(first_arg, "-n") == 0) {
                newline = 0;
                start_idx = 1;
            }
            free(first_arg);
        }

        for (int i = start_idx; i < node->data.simple_command.suffix_count; i++) {
            char *arg = expand_assignment(node->data.simple_command.suffix[i], env, ft, last_exit_status);
            char *equals = strchr(arg, '=');
            if (equals) *equals = '\0';
            printf("%s", equals ? equals + 1 : arg);
            if (i < node->data.simple_command.suffix_count - 1) printf(" ");
            free(arg);
        }
        if (newline) printf("\n");
        fflush(stdout);
        *last_exit_status = 0;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, "exit") == 0) {
        int exit_status = *last_exit_status;
        if (node->data.simple_command.suffix_count > 1) {
            fprintf(stderr, "exit: too many arguments\n");
            *last_exit_status = 1;
            return EXEC_NORMAL;
        }
        if (node->data.simple_command.suffix_count == 1) {
            char *arg = expand_assignment(node->data.simple_command.suffix[0], env, ft, last_exit_status);
            char *equals = strchr(arg, '=');
            if (equals) *equals = '\0';
            char *endptr;
            long status = strtol(arg, &endptr, 10);
            if (*endptr != '\0' || arg == endptr) {
                fprintf(stderr, "exit: numeric argument required\n");
                free(arg);
                exit(1);
            }
            exit_status = (int)(status & 0xFF);
            free(arg);
        }
        exit(exit_status);
    }
    if (strcmp(node->data.simple_command.command, "cd") == 0) {
        fprintf(stderr, "cd: changing directories is not implemented in C23\n");
        *last_exit_status = 1;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, "shift") == 0) {
        int shift_count = 1;
        if (node->data.simple_command.suffix_count > 1) {
            fprintf(stderr, "shift: too many arguments\n");
            *last_exit_status = 1;
            return EXEC_NORMAL;
        }
        if (node->data.simple_command.suffix_count == 1) {
            char *arg = expand_assignment(node->data.simple_command.suffix[0], env, ft, last_exit_status);
            char *equals = strchr(arg, '=');
            if (equals) *equals = '\0';
            char *endptr;
            long n = strtol(arg, &endptr, 10);
            if (*endptr != '\0' || arg == endptr || n < 0) {
                fprintf(stderr, "shift: invalid number\n");
                free(arg);
                *last_exit_status = 1;
                return EXEC_NORMAL;
            }
            shift_count = (int)n;
            free(arg);
        }
        if (shift_count > env->arg_count) {
            fprintf(stderr, "shift: shift count out of range\n");
            *last_exit_status = 1;
            return EXEC_NORMAL;
        }
        for (int i = 0; i < shift_count; i++) {
            free(env->args[i]);
        }
        for (int i = 0; i < env->arg_count - shift_count; i++) {
            env->args[i] = env->args[i + shift_count];
        }
        env->arg_count -= shift_count;
        *last_exit_status = 0;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, ":") == 0) {
        *last_exit_status = 0;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, ".") == 0) {
        if (node->data.simple_command.suffix_count != 1) {
            fprintf(stderr, ".: exactly one argument required\n");
            *last_exit_status = 2;
            return EXEC_NORMAL;
        }
        char *filename = expand_assignment(node->data.simple_command.suffix[0], env, ft, last_exit_status);
        char *equals = strchr(filename, '=');
        if (equals) *equals = '\0';

        char *full_path = find_file_in_path(filename, get_path(env));
        if (!full_path) {
            fprintf(stderr, ".: %s: cannot open file\n", filename);
            free(filename);
            *last_exit_status = 2;
            return EXEC_NORMAL;
        }

        FILE *file = fopen(full_path, "r");
        if (!file) {
            fprintf(stderr, ".: %s: cannot open file\n", full_path);
            free(full_path);
            free(filename);
            *last_exit_status = 2;
            return EXEC_NORMAL;
        }

        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        char *buffer = malloc(size + 1);
        if (!buffer) {
            fprintf(stderr, ".: memory allocation failed\n");
            fclose(file);
            free(full_path);
            free(filename);
            *last_exit_status = 2;
            return EXEC_NORMAL;
        }
        size_t read_size = fread(buffer, 1, size, file);
        buffer[read_size] = '\0';
        fclose(file);

        ParserState script_state;
        init_parser_state(&script_state);
        script_state.in_dot_script = 1;
        char *line = buffer;
        char *next_line;
        while (line && *line) {
            next_line = strchr(line, '\n');
            if (next_line) *next_line++ = '\0';
            
            ASTNode *ast = NULL;
            ParseStatus status = parse_line(line, &script_state, &ast);
            if (status == PARSE_COMPLETE && ast) {
                ExecStatus exec_status = execute_ast(ast, env, ft, last_exit_status);
                free_ast(ast);
                if (exec_status == EXEC_RETURN) {
                    free_parser_state(&script_state);
                    free(buffer);
                    free(full_path);
                    free(filename);
                    return EXEC_RETURN;
                }
            } else if (status == PARSE_ERROR) {
                fprintf(stderr, ".: parse error in %s\n", full_path);
                free_parser_state(&script_state);
                free(buffer);
                free(full_path);
                free(filename);
                *last_exit_status = 2;
                return EXEC_NORMAL;
            }
            line = next_line;
        }
        free_parser_state(&script_state);
        free(buffer);
        free(full_path);
        free(filename);
        *last_exit_status = 0;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, "return") == 0) {
        extern ParserState *current_parser_state;
        int return_status = 0;
        if (node->data.simple_command.suffix_count > 1) {
            fprintf(stderr, "return: too many arguments\n");
            *last_exit_status = 1;
            return EXEC_NORMAL;
        }
        if (node->data.simple_command.suffix_count == 1) {
            char *arg = expand_assignment(node->data.simple_command.suffix[0], env, ft, last_exit_status);
            char *equals = strchr(arg, '=');
            if (equals) *equals = '\0';
            char *endptr;
            long status = strtol(arg, &endptr, 10);
            if (*endptr != '\0' || arg == endptr) {
                fprintf(stderr, "return: numeric argument required\n");
                *last_exit_status = 1;
                free(arg);
                return EXEC_NORMAL;
            }
            return_status = (int)(status & 0xFF);
            free(arg);
        } else {
            return_status = *last_exit_status;
        }

        if (current_parser_state && (current_parser_state->in_function || current_parser_state->in_dot_script)) {
            *last_exit_status = return_status;
            return EXEC_RETURN;
        } else {
            exit(return_status);
        }
    }
    if (strcmp(node->data.simple_command.command, "true") == 0) {
        *last_exit_status = 0;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, "false") == 0) {
        *last_exit_status = 1;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, "pwd") == 0) {
        fprintf(stderr, "pwd: this shell has no ability to determine the working directory in C23\n");
        *last_exit_status = 1;
        return EXEC_NORMAL;
    }
    if (strcmp(node->data.simple_command.command, "test") == 0 || strcmp(node->data.simple_command.command, "[") == 0) {
        int argc = 1 + node->data.simple_command.suffix_count;
        char *argv[argc + 1];
        argv[0] = node->data.simple_command.command;
        for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
            argv[i + 1] = expand_assignment(node->data.simple_command.suffix[i], env, ft, last_exit_status);
            char *equals = strchr(argv[i + 1], '=');
            if (equals) *equals = '\0';
        }
        *last_exit_status = evaluate_test(argc, argv, last_exit_status);
        for (int i = 1; i < argc; i++) free(argv[i]);
        return EXEC_NORMAL;
    }

    if (has_redirects(node->data.simple_command.redirects)) {
        fprintf(stderr, "Error: I/O redirection not supported in C23\n");
        *last_exit_status = 1;
        return EXEC_NORMAL;
    }

    ASTNode *func_body = get_function_body(ft, node->data.simple_command.command);
    if (func_body) {
        if (has_redirects(get_function_redirects(ft, node->data.simple_command.command))) {
            fprintf(stderr, "Error: Function %s uses unsupported I/O redirection\n", node->data.simple_command.command);
            *last_exit_status = 1;
            return EXEC_NORMAL;
        }
        for (int i = 0; i < ft->func_count; i++) {
            if (strcmp(ft->functions[i].name, node->data.simple_command.command) == 0) {
                ft->functions[i].active = 1;
                ParserState *prev_state = current_parser_state;
                ParserState func_state;
                init_parser_state(&func_state);
                func_state.in_function = 1;
                current_parser_state = &func_state;
                ExecStatus status = execute_ast(func_body, env, ft, last_exit_status);
                current_parser_state = prev_state;
                ft->functions[i].active = 0;
                free_parser_state(&func_state);
                if (status == EXEC_RETURN) return EXEC_RETURN;
                break;
            }
        }
        return EXEC_NORMAL;
    }

    char command[MAX_COMMAND_LEN] = {0};
    char *interpreter = get_shebang_interpreter(node->data.simple_command.command);
    if (interpreter) {
        strncat(command, interpreter, MAX_COMMAND_LEN - strlen(command) - 1);
        strncat(command, " ", MAX_COMMAND_LEN - strlen(command) - 1);
        free(interpreter);
    }
    strncat(command, node->data.simple_command.command, MAX_COMMAND_LEN - strlen(command) - 1);
    for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
        char *expanded = expand_assignment(node->data.simple_command.suffix[i], env, ft, last_exit_status);
        char *equals = strchr(expanded, '=');
        if (equals) *equals = '\0';
        strncat(command, " ", MAX_COMMAND_LEN - strlen(command) - 1);
        strncat(command, equals ? equals + 1 : expanded, MAX_COMMAND_LEN - strlen(command) - 1);
        free(expanded);
    }

    int status = system(command);
    if (status == -1) {
        perror("system failed");
        *last_exit_status = -1;
    } else if (status == 0) {
        *last_exit_status = 0;
    } else {
        *last_exit_status = status & 0xFF;
    }
    return EXEC_NORMAL;
}


