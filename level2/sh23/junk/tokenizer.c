#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "tokenizer.h"
#include "logging.h"
#include "zstring.h"

#define nullptr NULL

// Helper function signatures (assumed implemented elsewhere)
static bool is_all_digits(czstring s);
static bool is_valid_name(czstring s);
static TokenType get_reserved_word_type(czstring s);
static int substitute_alias(czstring name, czstring value, int max_len);
static const char *parse_expansion(const char *p, char *current_token, int *pos, int max_len,
                                  int in_double_quote, int depth, char **active_aliases, int active_alias_count);

static bool is_all_digits(czstring s)
{
    Expects_not_null(s);
    Expects_gt(strlen(s), 0);

    size_t len = strlen (s);
    for (size_t i = 0; i < len; i ++)
        if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

static bool is_valid_name(czstring s) {
    Expects_not_null (s);
    Expects_gt (strlen(s), 0);

    if ((!isalpha((unsigned char)s[0]) && s[0] != '_')) return false;
    for (int i = 1; s[i]; i++) {
        if (!isalnum((unsigned char)s[i]) && s[i] != '_') return false;
    }
    return true;
}

static int is_operator_char(char c) {
    return c == ';' || c == '&' || c == '|' || c == '<' || c == '>' ||
           c == '(' || c == ')' || c == '{' || c == '}' || c == '-';
}

static int is_operator(const char *s) {
    if (!s[0]) return 0;
    if (s[1] == '\0') {
        return s[0] == ';' || s[0] == '&' || s[0] == '|' || s[0] == '<' ||
               s[0] == '>' || s[0] == '(' || s[0] == ')' || s[0] == '{' ||
               s[0] == '}';
    }
    if (s[2] == '\0') {
        return (strcmp(s, "&&") == 0 || strcmp(s, "||") == 0 ||
                strcmp(s, ">>") == 0 || strcmp(s, "<<") == 0 ||
                strcmp(s, "<>") == 0 || strcmp(s, ">|") == 0 ||
                strcmp(s, ";;") == 0 || strcmp(s, "<<-") == 0);
    }
    return 0;
}

static TokenType get_operator_type(const char *s) {
    if (strcmp(s, "<<") == 0) return DLESS;
    if (strcmp(s, "<<-") == 0) return DLESSDASH;
    return OPERATOR;
}

#define CHECK_TOKEN_LEN(status, input, p) do { \
    if (*pos >= MAX_TOKEN_LEN - 1) { \
        log_error("Token length exceeds %d characters at position %ld", \
                  MAX_TOKEN_LEN - 1, (long)(p - input)); \
        status = TOKENIZE_ERROR; \
        return p; \
    } \
} while (0)

#define CHECK_TOKEN_COUNT(status, input, p) do { \
    if (output->count >= MAX_TOKENS) { \
        log_error("Token count exceeds %d at token '%s'", \
                  MAX_TOKENS, current_token); \
        status = TOKENIZE_ERROR; \
        return p; \
    } \
} while (0)

#define CHECK_HEREDOC_CONTENT(status, input, p) do { \
    if (heredoc->content_pos >= MAX_HEREDOC_CONTENT - 1) { \
        log_error("Heredoc content exceeds %d characters", \
                  MAX_HEREDOC_CONTENT - 1); \
        status = TOKENIZE_ERROR; \
        return p; \
    } \
} while (0)

static const char *handle_comment(const char *p) {
    while (*p && *p != '\n') {
        if (*p == '\\' && *(p + 1) == '\n') {
            p += 2;
            continue;
        }
        p++;
    }
    if (*p == '\n') p++;
    return p;
}

static const char *handle_heredoc(const char *p, HeredocState *heredoc, TokenizeStatus *status, const char *input) {
    const char *line_start = p;
    int len = 0;
    while (*p && *p != '\n' && len < MAX_TOKEN_LEN - 1) {
        CHECK_HEREDOC_CONTENT(*status, input, p);
        heredoc->content[heredoc->content_pos++] = *p++;
        len++;
    }
    heredoc->content[heredoc->content_pos] = '\0';

    char line_store[MAX_TOKEN_LEN];
    char *line = &line_store[0];
    strncpy(line, line_start, len);
    line[len] = '\0';

    if (heredoc->strip_tabs) {
        while (*line == '\t') {
            line++;
            len--;
        }
    }

    if (len == strlen(heredoc->delimiter) && strcmp(line, heredoc->delimiter) == 0) {
        heredoc->active = 0;
        heredoc->content_pos -= len;
        heredoc->content[heredoc->content_pos] = '\0';
        if (*p == '\n') p++;
        *status = TOKENIZE_COMPLETE;
    } else {
        if (*p == '\n') {
            CHECK_HEREDOC_CONTENT(*status, input, p);
            heredoc->content[heredoc->content_pos++] = *p++;
            heredoc->content[heredoc->content_pos] = '\0';
        }
        *status = TOKENIZE_HEREDOC;
    }

    return p;
}

static const char *handle_quotes(const char *p, char *current_token, int *pos, int *in_word,
                                TokenList *output, int depth, char **active_aliases, 
                                int active_alias_count, int *in_single_quote, int *in_double_quote,
                                const char *input, TokenizeStatus *status) {
    if (!*in_word) *in_word = 1;

    if (*p == '\\') {
        CHECK_TOKEN_LEN(*status, input, p);
        current_token[(*pos)++] = *p++;
        if (*p && *p != '\n') {
            CHECK_TOKEN_LEN(*status, input, p);
            current_token[(*pos)++] = *p++;
        } else if (*p == '\n') {
            p++;
        }
        return p;
    }

    if (*p == '\'') {
        *in_single_quote = 1;
        CHECK_TOKEN_LEN(*status, input, p);
        current_token[(*pos)++] = *p++;
        while (*p && *p != '\'') {
            CHECK_TOKEN_LEN(*status, input, p);
            current_token[(*pos)++] = *p++;
        }
        if (*p == '\'') {
            CHECK_TOKEN_LEN(*status, input, p);
            current_token[(*pos)++] = *p++;
            *in_single_quote = 0;
        } else {
            log_error("Unmatched single quote at position %ld", (long)(p - input));
            return NULL;
        }
        return p;
    }

    if (*p == '"') {
        *in_double_quote = 1;
        CHECK_TOKEN_LEN(*status, input, p);
        current_token[(*pos)++] = *p++;
        while (*p && *p != '"') {
            if (*p == '\\') {
                CHECK_TOKEN_LEN(*status, input, p);
                current_token[(*pos)++] = *p++;
                if (*p && strchr("$`\"\\", *p)) {
                    CHECK_TOKEN_LEN(*status, input, p);
                    current_token[(*pos)++] = *p++;
                } else if (*p == '\n') {
                    p++;
                } else if (*p) {
                    CHECK_TOKEN_LEN(*status, input, p);
                    current_token[(*pos)++] = *p++;
                }
            } else if (*p == '$' || *p == '`') {
                p = parse_expansion(p, current_token, pos, MAX_TOKEN_LEN, 1, depth, 
                                   active_aliases, active_alias_count);
                if (!p) return NULL;
            } else if (*p == '@') {
                CHECK_TOKEN_LEN(*status, input, p);
                current_token[(*pos)++] = *p++;
            } else {
                CHECK_TOKEN_LEN(*status, input, p);
                current_token[(*pos)++] = *p++;
            }
        }
        if (*p == '"') {
            CHECK_TOKEN_LEN(*status, input, p);
            current_token[(*pos)++] = *p++;
            *in_double_quote = 0;
        } else {
            log_error("Unmatched double quote at position %ld", (long)(p - input));
            return NULL;
        }
        return p;
    }

    return p;
}

static const char *handle_operator(const char *p, char *current_token, int *pos, int *in_word, 
                                  TokenList *output, TokenizeStatus *status, HeredocState *heredoc, 
                                  const char *input) {
    if (*in_word) {
        current_token[*pos] = '\0';
        CHECK_TOKEN_COUNT(*status, input, p);
        strncpy(output->tokens[output->count].text, current_token, MAX_TOKEN_LEN);
        (output->count)++;
        *pos = 0;
        *in_word = 0;
    }

    CHECK_TOKEN_LEN(*status, input, p);
    current_token[(*pos)++] = *p++;
    if (*p && is_operator_char(*p)) {
        current_token[*pos] = *p;
        current_token[*pos + 1] = '\0';
        if (is_operator(current_token)) {
            CHECK_TOKEN_LEN(*status, input, p);
            current_token[(*pos)++] = *p++;
        }
    }

    current_token[*pos] = '\0';
    if (is_operator(current_token)) {
        CHECK_TOKEN_COUNT(*status, input, p);
        strncpy(output->tokens[output->count].text, current_token, MAX_TOKEN_LEN);
        TokenType type = get_operator_type(current_token);
        output->tokens[output->count].type = type;
        (output->count)++;
        *pos = 0;

        if (type == DLESS || type == DLESSDASH) {
            char delimiter[MAX_TOKEN_LEN] = {0};
            int delim_pos = 0;
            int quoted = 0;

            while (*p && isspace(*p) && *p != '\n') p++;

            if (*p == '\'' || *p == '"') {
                quoted = 1;
                char quote = *p++;
                while (*p && *p != quote && delim_pos < MAX_TOKEN_LEN - 1) {
                    delimiter[delim_pos++] = *p++;
                }
                if (*p == quote) p++;
                else {
                    log_error("Unmatched quote in heredoc delimiter at position %ld",
                              (long)(p - input));
                    *status = TOKENIZE_ERROR;
                    return p;
                }
            } else {
                while (*p && !isspace(*p) && !is_operator_char(*p) && delim_pos < MAX_TOKEN_LEN - 1) {
                    delimiter[delim_pos++] = *p++;
                }
            }
            delimiter[delim_pos] = '\0';

            if (delim_pos == 0) {
                log_error("Missing heredoc delimiter at position %ld", (long)(p - input));
                *status = TOKENIZE_ERROR;
                return p;
            }

            heredoc->active = 1;
            strncpy(heredoc->delimiter, delimiter, MAX_TOKEN_LEN);
            heredoc->quoted = quoted;
            heredoc->strip_tabs = (type == DLESSDASH);
            heredoc->content_pos = 0;
            heredoc->content[0] = '\0';
            *status = TOKENIZE_HEREDOC;
        }
    } else {
        CHECK_TOKEN_COUNT(*status, input, p);
        strncpy(output->tokens[output->count].text, current_token, MAX_TOKEN_LEN);
        output->tokens[output->count].type = TOKEN;
        (output->count)++;
        *pos = 0;
    }

    return p;
}

static void assign_token_types(Tokenizer *t) {
    for (int i = 0; i < t->tokens.count; i++) {
        if (t->tokens.tokens[i].type != OPERATOR &&
            i + 1 < t->tokens.count && is_all_digits(t->tokens.tokens[i].text) &&
            (strcmp(t->tokens.tokens[i + 1].text, "<") == 0 || strcmp(t->tokens.tokens[i + 1].text, ">") == 0)) {
            t->tokens.tokens[i].type = IO_NUMBER;
        } else if (t->tokens.tokens[i].type != OPERATOR) {
            t->tokens.tokens[i].type = TOKEN;
        }
    }

    for (int i = 0; i < t->tokens.count; i++) {
        if (t->tokens.tokens[i].type == TOKEN) {
            if (i == 0 || (i > 0 && is_operator(t->tokens.tokens[i - 1].text))) {
                TokenType reserved = get_reserved_word_type(t->tokens.tokens[i].text);
                if (reserved != TOKEN) {
                    t->tokens.tokens[i].type = reserved;
                } else if (i + 1 < t->tokens.count && 
                           (t->tokens.tokens[i + 1].type == DO || t->tokens.tokens[i + 1].type == LPAREN) &&
                           is_valid_name(t->tokens.tokens[i].text)) {
                    t->tokens.tokens[i].type = NAME;
                } else {
                    t->tokens.tokens[i].type = WORD;
                }
            } else if (i > 0 && t->tokens.tokens[i - 1].type != ASSIGNMENT_WORD &&
                       strchr(t->tokens.tokens[i].text, '=')) {
                char *eq = strchr(t->tokens.tokens[i].text, '=');
                if (eq != t->tokens.tokens[i].text) {
                    *eq = '\0';
                    if (is_valid_name(t->tokens.tokens[i].text)) {
                        t->tokens.tokens[i].type = ASSIGNMENT_WORD;
                    }
                    *eq = '=';
                }
                if (t->tokens.tokens[i].type == TOKEN) {
                    t->tokens.tokens[i].type = WORD;
                }
            } else {
                t->tokens.tokens[i].type = WORD;
            }
        }
    }
}

static void substitute_aliases(TokenList *output, int depth, char **active_aliases, int active_alias_count) {
    int perform_alias_check = 0;

    for (int i = 0; i < output->count;) {
        char alias_value[MAX_TOKEN_LEN];
        TokenList temp_output = { .count = 0 };

        if (!perform_alias_check && 
            output->tokens[i].type != WORD && output->tokens[i].type != NAME) {
            i++;
            continue;
        }

        if (depth >= MAX_ALIAS_DEPTH) {
            log_error("Alias recursion limit exceeded for '%s'", output->tokens[i].text);
            i++;
            perform_alias_check = 0;
            continue;
        }

        for (int j = 0; j < active_alias_count; j++) {
            if (strcmp(active_aliases[j], output->tokens[i].text) == 0) {
                i++;
                perform_alias_check = 0;
                break;
            }
        }
        if (!perform_alias_check && 
            output->tokens[i].type != WORD && output->tokens[i].type != NAME) {
            i++;
            continue;
        }

        if (substitute_alias(output->tokens[i].text, alias_value, MAX_TOKEN_LEN)) {
            if (active_alias_count >= MAX_ALIAS_DEPTH) {
                log_error("Too many active aliases for '%s'", output->tokens[i].text);
                i++;
                perform_alias_check = 0;
                continue;
            }
            active_aliases[active_alias_count] = output->tokens[i].text;
            int new_active_alias_count = active_alias_count + 1;

            tokenizer_add_line(alias_value, &temp_output, depth + 1, active_aliases, new_active_alias_count);

            int shift_amount = temp_output.count - 1;
            if (shift_amount > 0) {
                if (output->count + shift_amount >= MAX_TOKENS) {
                    log_error("Token count exceeds %d after alias expansion of '%s'",
                              MAX_TOKENS, output->tokens[i].text);
                    return;
                }
                for (int j = output->count - 1; j > i; j--) {
                    output->tokens[j + shift_amount] = output->tokens[j];
                }
            }

            for (int j = 0; j < temp_output.count; j++) {
                output->tokens[i + j] = temp_output.tokens[j];
            }
            output->count += shift_amount;

            size_t alias_len = strlen(alias_value);
            if (temp_output.count > 0 && alias_len > 0 && 
                (alias_value[alias_len - 1] == ' ' || alias_value[alias_len - 1] == '\t') &&
                i + temp_output.count < output->count) {
                i += temp_output.count;
                perform_alias_check = 1;
            } else {
                i += temp_output.count;
                perform_alias_check = 0;
            }
        } else {
            i++;
            perform_alias_check = 0;
        }
    }
}

static void parse_tokens(Tokenizer *t); //const char *input, TokenList *output, char *current_token, int *pos, 
                        // int *in_word, TokenizeStatus *status, TokenizerState *state) {
{
    const char *p = t->input;
    int in_single_quote = 0;
    int in_double_quote = 0;

    if (t->heredoc.active) {
        p = handle_heredoc(p, &t->heredoc, t->status, t->input);
        return;
    }

    while (*p) {
        if (*p == '\0') {
            if (*in_word) {
                current_token[*pos] = '\0';
                // CHECK_TOKEN_COUNT(*status, input, p);
                strncpy(t->tokens[output->count].text, current_token, MAX_TOKEN_LEN);
                (t->tokens.count)++;
            }
            break;
        }

        if (*p < 32 && !in_single_quote && !in_double_quote && 
            !(*pos > 0 && current_token[*pos - 1] == '\\')) {
            if (*p != '\n' && *p != '\t' && *p != '\r') {
                if (*p == '\a') {
                    log_warning("Unquoted BEL character at position %ld",
                                (long)(p - input));
                } else {
                    log_error("Invalid control character (ASCII %d) at position %ld",
                              (int)*p, (long)(p - input));
                    *status = TOKENIZE_ERROR;
                    return;
                }
            }
        }

        if (*p == '#' && !in_single_quote && !in_double_quote && 
            !(*pos > 0 && current_token[*pos - 1] == '\\')) {
            p = handle_comment(p);
            continue;
        }

        if (*p == '\n' && !in_single_quote && !in_double_quote) {
            current_token[*pos] = '\0';
            if (*pos > 0) {
                CHECK_TOKEN_COUNT(*status, input, p);
                strncpy(output->tokens[output->count].text, current_token, MAX_TOKEN_LEN);
                (output->count)++;
            }
            CHECK_TOKEN_COUNT(*status, input, p);
            strncpy(output->tokens[output->count].text, "\n", MAX_TOKEN_LEN);
            output->tokens[output->count].type = NEWLINE;
            (output->count)++;
            *pos = 0;
            p++;
            continue;
        }

        if (*p == '\\' || *p == '\'' || *p == '"') {
            p = handle_quotes(p, current_token, pos, in_word, output, depth, active_aliases, 
                             active_alias_count, &in_single_quote, &in_double_quote, input, status);
            if (!p) {
                log_error("Quote parsing failed at position %ld", (long)(p - input));
                *status = TOKENIZE_ERROR;
                return;
            }
            continue;
        }

        if ((*p == '$' || *p == '`') && !*in_word && !in_single_quote) {
            if (!*in_word) *in_word = 1;
            p = parse_expansion(p, current_token, pos, MAX_TOKEN_LEN, 0, depth, active_aliases, active_alias_count);
            if (!p) {
                log_error("Invalid parameter expansion at position %ld", (long)(p - input));
                *status = TOKENIZE_ERROR;
                return;
            }
            continue;
        }

        if (isspace(*p) && *p != '\n' && !in_single_quote && !in_double_quote) {
            if (*in_word) {
                current_token[*pos] = '\0';
                CHECK_TOKEN_COUNT(*status, input, p);
                strncpy(output->tokens[output->count].text, current_token, MAX_TOKEN_LEN);
                (output->count)++;
                *pos = 0;
                *in_word = 0;
            }
            p++;
            continue;
        }

        if (is_operator_char(*p) && !in_single_quote && !in_double_quote) {
            p = handle_operator(p, current_token, pos, in_word, output, status, &state->heredoc, input);
            continue;
        }

        if (!*in_word) *in_word = 1;
        CHECK_TOKEN_LEN(*status, input, p);
        current_token[(*pos)++] = *p++;
    }

    if (*in_word) {
        current_token[*pos] = '\0';
        CHECK_TOKEN_COUNT(*status, input, p);
        strncpy(output->tokens[output->count].text, current_token, MAX_TOKEN_LEN);
        (output->count)++;
    }
}

TokenizeStatus tokenizer_add_line(Tokenizer *t, const char *line)
{
    char *current_token;
    Expects(line);
    Expects(strlen(line) > 0);
    Expects(strlen(line) < MAX_LINE_LEN);

    free (t->input.line);
    strcpy(t->input.line, line);
    t->input.pos = 0;
    t->input.len = strlen(t->input.line);
    current_token = line;

    parse_tokens(t);
    if (t->status == TOKENIZE_ERROR)
        return TOKENIZE_ERROR;

    if (t->status != TOKENIZE_HEREDOC && t->status != TOKENIZE_LINE_CONTINUATION)
    {
        assign_token_types(t);
        substitute_aliases(t);
    }

    return t->status;
}
