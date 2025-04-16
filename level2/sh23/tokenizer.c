#include "tokenizer.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

struct Tokenizer {
    String *current_token;
    TokenArray *tokens;
    String *heredoc_delim;
    String *heredoc_content;
    int in_quotes;
    int in_dquotes;
    int escaped;
    int is_first_word;
    int after_heredoc_op;
    int paren_depth_dparen;
    int paren_depth_arith;
    int in_backtick;
    int brace_depth_param;
};

// Validate a name (POSIX: alphanumeric or underscore, not starting with digit)
int is_valid_name_cstr(const char *name)
{
    if (!name || !*name || isdigit(*name))
        return 0;
    for (const char *p = name; *p; p++)
    {
        if (!isalnum(*p) && *p != '_')
            return 0;
    }
    return 1;
}

int is_valid_name(const String *name)
{
    if (!name || string_is_empty(name))
        return 0;
    return is_valid_name_cstr(string_data(name));
}

// Validate a number (all digits)
int is_number_cstr(const char *text)
{
    if (!text || !*text)
        return 0;
    for (const char *p = text; *p; p++)
    {
        if (!isdigit(*p))
            return 0;
    }
    return 1;
}

int is_number(const String *text)
{
    if (!text || string_is_empty(text))
        return 0;
    return is_number_cstr(string_data(text));
}

// Helper: Check if a character is an operator start
static int is_operator_char(char c)
{
    return c == ';' || c == '|' || c == '&' || c == '<' || c == '>' || c == '(' || c == ')';
}

// Helper: Check if a string is a keyword
static int is_keyword_cstr(const char *text)
{
    const char *keywords[] = {
        "if", "then", "else", "elif", "fi",
        "while", "do", "done", "until",
        "for", "in", "case", "esac",
        "break", "continue", "return", NULL};
    for (int i = 0; keywords[i]; i++)
    {
        if (strcmp(text, keywords[i]) == 0)
            return 1;
    }
    return 0;
}

static int is_keyword_string(const String *text)
{
    return is_keyword_cstr(string_data(text));
}

// Helper: Process operator
static int process_operator(Tokenizer *tokenizer, const char **p, String *current_token)
{
    if (!tokenizer || !p || !*p || !current_token)
        return -1;

    const char *start = *p;
    TokenType type;

    if (strncmp(start, "&&", 2) == 0) {
        type = TOKEN_AND_IF;
        *p += 2;
    } else if (strncmp(start, "||", 2) == 0) {
        type = TOKEN_OR_IF;
        *p += 2;
    } else if (strncmp(start, ";;", 2) == 0) {
        type = TOKEN_DSEMI;
        *p += 2;
    } else if (strncmp(start, "<<-", 3) == 0) {
        type = TOKEN_DLESSDASH;
        *p += 3;
    } else if (strncmp(start, "<<", 2) == 0) {
        type = TOKEN_DLESS;
        *p += 2;
    } else if (strncmp(start, ">>", 2) == 0) {
        type = TOKEN_DGREAT;
        *p += 2;
    } else if (strncmp(start, "<&", 2) == 0) {
        type = TOKEN_LESSAND;
        *p += 2;
    } else if (strncmp(start, ">&", 2) == 0) {
        type = TOKEN_GREATAND;
        *p += 2;
    } else if (strncmp(start, "<>", 2) == 0) {
        type = TOKEN_LESSGREAT;
        *p += 2;
    } else if (strncmp(start, ">|", 2) == 0) {
        type = TOKEN_CLOBBER;
        *p += 2;
    } else if (*start == '&') {
        type = TOKEN_AMP;
        *p += 1;
    } else if (*start == ';') {
        type = TOKEN_SEMI;
        *p += 1;
    } else if (*start == '<' || *start == '>' || *start == '|' || *start == '(' || *start == ')') {
        type = TOKEN_OPERATOR;
        *p += 1;
    } else {
        return -1;
    }

    size_t len = *p - start;
    char op_str[4];
    if (len >= sizeof(op_str)) {
        log_fatal("process_operator: operator too long");
        return -1;
    }
    strncpy(op_str, start, len);
    op_str[len] = '\0';

    if (tokenizer_set_current_token_cstr(tokenizer, op_str) != 0 ||
        tokenizer_add_token_cstr(tokenizer, type, op_str) != 0) {
        log_fatal("process_operator: failed to set or add operator token");
        return -1;
    }

    tokenizer_set_after_heredoc_op(tokenizer, type == TOKEN_DLESS || type == TOKEN_DLESSDASH);
    tokenizer_set_is_first_word(tokenizer, type == TOKEN_OPERATOR || type == TOKEN_AND_IF ||
                               type == TOKEN_OR_IF || type == TOKEN_DSEMI);
    return 0;
}

// Helper: Process here-document delimiter
static int process_heredoc(Tokenizer *tokenizer, const char **p)
{
    if (!tokenizer || !p || !*p)
        return -1;

    if (!string_is_empty(tokenizer->current_token)) {
        if (tokenizer_set_heredoc_delim(tokenizer, tokenizer->current_token) != 0 ||
            tokenizer_add_token_cstr(tokenizer, TOKEN_HEREDOC_DELIM, string_data(tokenizer->current_token)) != 0) {
            log_fatal("process_heredoc: failed to set heredoc delimiter");
            return -1;
        }
        if (tokenizer_set_current_token_cstr(tokenizer, "") != 0) {
            log_fatal("process_heredoc: failed to clear current_token");
            return -1;
        }
        tokenizer->after_heredoc_op = 0;
    } else {
        log_fatal("process_heredoc: no delimiter");
        return -1;
    }

    // Skip whitespace until newline
    while (*p[0] && *p[0] != '\n' && isspace(*p[0]))
        (*p)++;
    if (*p && *p[0] == '\n') {
        (*p)++; // Consume newline
    } else {
        log_fatal("process_heredoc: expected newline after delimiter");
        return -1;
    }

    if (!tokenizer->heredoc_content) {
        if (tokenizer_set_heredoc_content_cstr(tokenizer, "") != 0) {
            log_fatal("process_heredoc: failed to initialize heredoc_content");
            return -1;
        }
    }

    return 0;
}

// Helper: Process here-document content
static int process_heredoc_content(Tokenizer *tokenizer, const char *input)
{
    if (!tokenizer || !input || !tokenizer->heredoc_delim)
        return -1;

    size_t len = strlen(input);
    char *line = malloc(len + 1);
    if (!line) {
        log_fatal("process_heredoc_content: out of memory");
        return -1;
    }
    strcpy(line, input);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
        len--;
    }

    const char *delim = string_data(tokenizer->heredoc_delim);
    if (strcmp(line, delim) == 0) {
        if (!string_is_empty(tokenizer->heredoc_content)) {
            if (tokenizer_add_token_cstr(tokenizer, TOKEN_WORD, string_data(tokenizer->heredoc_content)) != 0) {
                log_fatal("process_heredoc_content: failed to add heredoc content token");
                free(line);
                return -1;
            }
        }
        if (tokenizer_set_heredoc_content(tokenizer, NULL) != 0 ||
            tokenizer_set_heredoc_delim(tokenizer, NULL) != 0) {
            log_fatal("process_heredoc_content: failed to clear heredoc state");
            free(line);
            return -1;
        }
        free(line);
        return 0;
    }

    String *new_content = tokenizer->heredoc_content ? string_create_from(tokenizer->heredoc_content) : string_create_empty(0);
    if (!new_content) {
        log_fatal("process_heredoc_content: failed to create heredoc content");
        free(line);
        return -1;
    }
    if (!string_is_empty(new_content)) {
        if (string_append_ascii_char(new_content, '\n') != 0) {
            log_fatal("process_heredoc_content: failed to append newline");
            string_destroy(new_content);
            free(line);
            return -1;
        }
    }
    if (string_append_cstr(new_content, line) != 0) {
        log_fatal("process_heredoc_content: failed to append line");
        string_destroy(new_content);
        free(line);
        return -1;
    }
    if (tokenizer_set_heredoc_content(tokenizer, new_content) != 0) {
        log_fatal("process_heredoc_content: failed to set heredoc content");
        string_destroy(new_content);
        free(line);
        return -1;
    }
    string_destroy(new_content);
    free(line);
    return 0;
}

// Helper: Process command substitution $(...)
static int process_dparen(Tokenizer *tokenizer, const char **p)
{
    if (!tokenizer || !p || !*p)
        return -1;

    String *current = tokenizer_get_current_token(tokenizer);
    if (!current) {
        log_fatal("process_dparen: no current token");
        return -1;
    }

    if (tokenizer_get_paren_depth_dparen(tokenizer) == 0) {
        if (string_append_cstr(current, "$(") != 0) {
            log_fatal("process_dparen: failed to append '$('");
            return -1;
        }
        *p += 2;
        tokenizer_set_paren_depth_dparen(tokenizer, 1);
    } else {
        if (string_append_ascii_char(current, '(') != 0) {
            log_fatal("process_dparen: failed to append '('");
            return -1;
        }
        (*p)++;
        tokenizer_set_paren_depth_dparen(tokenizer, tokenizer_get_paren_depth_dparen(tokenizer) + 1);
    }

    while (*p) {
        if (tokenizer_get_escaped(tokenizer)) {
            if (string_append_ascii_char(current, *p[0]) != 0) {
                log_fatal("process_dparen: failed to append escaped char");
                return -1;
            }
            tokenizer_set_escaped(tokenizer, 0);
            (*p)++;
            continue;
        }
        if (*p == '\\' && !tokenizer_get_in_quotes(tokenizer)) {
            if (string_append_ascii_char(current, '\\') != 0) {
                log_fatal("process_dparen: failed to append '\\'");
                return -1;
            }
            tokenizer_set_escaped(tokenizer, 1);
            (*p)++;
            continue;
        }
        if (*p == '\'' && !tokenizer_get_in_dquotes(tokenizer)) {
            tokenizer_set_in_quotes(tokenizer, !tokenizer_get_in_quotes(tokenizer));
            if (string_append_ascii_char(current, '\'') != 0) {
                log_fatal("process_dparen: failed to append '''");
                return -1;
            }
            (*p)++;
            continue;
        }
        if (*p == '"' && !tokenizer_get_in_quotes(tokenizer)) {
            tokenizer_set_in_dquotes(tokenizer, !tokenizer_get_in_dquotes(tokenizer));
            if (string_append_ascii_char(current, '"') != 0) {
                log_fatal("process_dparen: failed to append '\"'");
                return -1;
            }
            (*p)++;
            continue;
        }
        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer)) {
            if (*p == '(') {
                return process_dparen(tokenizer, p);
            } else if (*p == ')') {
                tokenizer_set_paren_depth_dparen(tokenizer, tokenizer_get_paren_depth_dparen(tokenizer) - 1);
                if (string_append_ascii_char(current, ')') != 0) {
                    log_fatal("process_dparen: failed to append ')'");
                    return -1;
                }
                (*p)++;
                if (tokenizer_get_paren_depth_dparen(tokenizer) == 0) {
                    if (tokenizer_add_token_cstr(tokenizer, TOKEN_DPAREN, string_data(current)) != 0 ||
                        tokenizer_set_current_token_cstr(tokenizer, "") != 0) {
                        log_fatal("process_dparen: failed to add DPAREN token");
                        return -1;
                    }
                    return 0;
                }
                continue;
            }
        }
        if (string_append_ascii_char(current, *p[0]) != 0) {
            log_fatal("process_dparen: failed to append char");
            return -1;
        }
        (*p)++;
    }

    return -1; // Unclosed $(...)
}

// Helper: Process command substitution `...`
static int process_backtick(Tokenizer *tokenizer, const char **p)
{
    if (!tokenizer || !p || !*p)
        return -1;

    String *current = tokenizer_get_current_token(tokenizer);
    if (!current) {
        log_fatal("process_backtick: no current token");
        return -1;
    }

    if (!tokenizer_get_in_backtick(tokenizer)) {
        if (string_append_ascii_char(current, '`') != 0) {
            log_fatal("process_backtick: failed to append '`'");
            return -1;
        }
        (*p)++;
        tokenizer_set_in_backtick(tokenizer, 1);
    } else {
        if (string_append_ascii_char(current, '`') != 0) {
            log_fatal("process_backtick: failed to append '`'");
            return -1;
        }
        (*p)++;
        tokenizer_set_in_backtick(tokenizer, 0);
        if (tokenizer_add_token_cstr(tokenizer, TOKEN_BACKTICK, string_data(current)) != 0 ||
            tokenizer_set_current_token_cstr(tokenizer, "") != 0) {
            log_fatal("process_backtick: failed to add BACKTICK token");
            return -1;
        }
        return 0;
    }

    while (*p) {
        if (tokenizer_get_escaped(tokenizer)) {
            if (string_append_ascii_char(current, *p[0]) != 0) {
                log_fatal("process_backtick: failed to append escaped char");
                return -1;
            }
            tokenizer_set_escaped(tokenizer, 0);
            (*p)++;
            continue;
        }
        if (*p == '\\') {
            if (string_append_ascii_char(current, '\\') != 0) {
                log_fatal("process_backtick: failed to append '\\'");
                return -1;
            }
            tokenizer_set_escaped(tokenizer, 1);
            (*p)++;
            continue;
        }
        if (*p == '\'' && !tokenizer_get_in_quotes(tokenizer)) {
            tokenizer_set_in_quotes(tokenizer, !tokenizer_get_in_quotes(tokenizer));
            if (string_append_ascii_char(current, '\'') != 0) {
                log_fatal("process_backtick: failed to append '''");
                return -1;
            }
            (*p)++;
            continue;
        }
        if (*p == '`' && !tokenizer_get_in_quotes(tokenizer)) {
            return process_backtick(tokenizer, p);
        }
        if (string_append_ascii_char(current, *p[0]) != 0) {
            log_fatal("process_backtick: failed to append char");
            return -1;
        }
        (*p)++;
    }

    return -1; // Unclosed `...
}

// Helper: Process arithmetic expansion $((...))
static int process_arith(Tokenizer *tokenizer, const char **p)
{
    if (!tokenizer || !p || !*p)
        return -1;

    String *current = tokenizer_get_current_token(tokenizer);
    if (!current) {
        log_fatal("process_arith: no current token");
        return -1;
    }

    if (tokenizer_get_paren_depth_arith(tokenizer) == 0) {
        if (string_append_cstr(current, "$((") != 0) {
            log_fatal("process_arith: failed to append '$)('");
            return -1;
        }
        *p += 3;
        tokenizer_set_paren_depth_arith(tokenizer, 2);
    } else {
        if (string_append_ascii_char(current, '(') != 0) {
            log_fatal("process_arith: failed to append '('");
            return -1;
        }
        (*p)++;
        tokenizer_set_paren_depth_arith(tokenizer, tokenizer_get_paren_depth_arith(tokenizer) + 1);
    }

    while (*p) {
        if (tokenizer_get_escaped(tokenizer)) {
            if (string_append_ascii_char(current, *p[0]) != 0) {
                log_fatal("process_arith: failed to append escaped char");
                return -1;
            }
            tokenizer_set_escaped(tokenizer, 0);
            (*p)++;
            continue;
        }
        if (*p == '\\' && !tokenizer_get_in_quotes(tokenizer)) {
            if (string_append_ascii_char(current, '\\') != 0) {
                log_fatal("process_arith: failed to append '\\'");
                return -1;
            }
            tokenizer_set_escaped(tokenizer, 1);
            (*p)++;
            continue;
        }
        if (*p == '\'' && !tokenizer_get_in_dquotes(tokenizer)) {
            tokenizer_set_in_quotes(tokenizer, !tokenizer_get_in_quotes(tokenizer));
            if (string_append_ascii_char(current, '\'') != 0) {
                log_fatal("process_arith: failed to append '''");
                return -1;
            }
            (*p)++;
            continue;
        }
        if (*p == '"' && !tokenizer_get_in_quotes(tokenizer)) {
            tokenizer_set_in_dquotes(tokenizer, !tokenizer_get_in_dquotes(tokenizer));
            if (string_append_ascii_char(current, '"') != 0) {
                log_fatal("process_arith: failed to append '\"'");
                return -1;
            }
            (*p)++;
            continue;
        }
        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer)) {
            if (*p == '(') {
                return process_arith(tokenizer, p);
            } else if (*p == ')') {
                tokenizer_set_paren_depth_arith(tokenizer, tokenizer_get_paren_depth_arith(tokenizer) - 1);
                if (string_append_ascii_char(current, ')') != 0) {
                    log_fatal("process_arith: failed to append ')'");
                    return -1;
                }
                (*p)++;
                if (tokenizer_get_paren_depth_arith(tokenizer) == 1) {
                    continue; // Expect second )
                }
                if (tokenizer_get_paren_depth_arith(tokenizer) == 0) {
                    if (tokenizer_add_token_cstr(tokenizer, TOKEN_ARITH, string_data(current)) != 0 ||
                        tokenizer_set_current_token_cstr(tokenizer, "") != 0) {
                        log_fatal("process_arith: failed to add ARITH token");
                        return -1;
                    }
                    return 0;
                }
            }
        }
        if (string_append_ascii_char(current, *p[0]) != 0) {
            log_fatal("process_arith: failed to append char");
            return -1;
        }
        (*p)++;
    }

    return -1; // Unclosed $((...)
}

// Helper: Process parameter expansion ${...}
static int process_param(Tokenizer *tokenizer, const char **p)
{
    if (!tokenizer || !p || !*p)
        return -1;

    String *current = tokenizer_get_current_token(tokenizer);
    if (!current) {
        log_fatal("process_param: no current token");
        return -1;
    }

    if (tokenizer_get_brace_depth_param(tokenizer) == 0) {
        if (string_append_cstr(current, "${") != 0) {
            log_fatal("process_param: failed to append '${'");
            return -1;
        }
        *p += 2;
        tokenizer_set_brace_depth_param(tokenizer, 1);
    } else {
        if (string_append_ascii_char(current, '{') != 0) {
            log_fatal("process_param: failed to append '{'");
            return -1;
        }
        (*p)++;
        tokenizer_set_brace_depth_param(tokenizer, tokenizer_get_brace_depth_param(tokenizer) + 1);
    }

    while (*p) {
        if (tokenizer_get_escaped(tokenizer)) {
            if (string_append_ascii_char(current, *p[0]) != 0) {
                log_fatal("process_param: failed to append escaped char");
                return -1;
            }
            tokenizer_set_escaped(tokenizer, 0);
            (*p)++;
            continue;
        }
        if (*p == '\\' && !tokenizer_get_in_quotes(tokenizer)) {
            if (string_append_ascii_char(current, '\\') != 0) {
                log_fatal("process_param: failed to append '\\'");
                return -1;
            }
            tokenizer_set_escaped(tokenizer, 1);
            (*p)++;
            continue;
        }
        if (*p == '\'' && !tokenizer_get_in_dquotes(tokenizer)) {
            tokenizer_set_in_quotes(tokenizer, !tokenizer_get_in_quotes(tokenizer));
            if (string_append_ascii_char(current, '\'') != 0) {
                log_fatal("process_param: failed to append '''");
                return -1;
            }
            (*p)++;
            continue;
        }
        if (*p == '"' && !tokenizer_get_in_quotes(tokenizer)) {
            tokenizer_set_in_dquotes(tokenizer, !tokenizer_get_in_dquotes(tokenizer));
            if (string_append_ascii_char(current, '"') != 0) {
                log_fatal("process_param: failed to append '\"'");
                return -1;
            }
            (*p)++;
            continue;
        }
        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer)) {
            if (*p == '{') {
                return process_param(tokenizer, p);
            } else if (*p == '}') {
                tokenizer_set_brace_depth_param(tokenizer, tokenizer_get_brace_depth_param(tokenizer) - 1);
                if (string_append_ascii_char(current, '}') != 0) {
                    log_fatal("process_param: failed to append '}'");
                    return -1;
                }
                (*p)++;
                if (tokenizer_get_brace_depth_param(tokenizer) == 0) {
                    if (tokenizer_add_token_cstr(tokenizer, TOKEN_PARAM, string_data(current)) != 0 ||
                        tokenizer_set_current_token_cstr(tokenizer, "") != 0) {
                        log_fatal("process_param: failed to add PARAM token");
                        return -1;
                    }
                    return 0;
                }
                continue;
            }
        }
        if (string_append_ascii_char(current, *p[0]) != 0) {
            log_fatal("process_param: failed to append char");
            return -1;
        }
        (*p)++;
    }

    return -1; // Unclosed ${...
}

// Helper: Finalize current_token as a token
static int finalize_current_token(Tokenizer *tokenizer)
{
    if (!tokenizer->current_token || string_is_empty(tokenizer->current_token))
        return 0;

    TokenType type = TOKEN_WORD;
    if (is_keyword_string(tokenizer->current_token)) {
        type = TOKEN_KEYWORD;
    } else if (is_valid_name(tokenizer->current_token)) {
        // Will check for assignment in post-process
        type = TOKEN_WORD;
    }

    if (tokenizer_add_token_cstr(tokenizer, type, string_data(tokenizer->current_token)) != 0) {
        log_fatal("finalize_current_token: failed to add token");
        return -1;
    }

    if (tokenizer_set_current_token_cstr(tokenizer, "") != 0) {
        log_fatal("finalize_current_token: failed to clear current_token");
        return -1;
    }
    tokenizer_set_is_first_word(tokenizer, 0);
    return 0;
}

// Constructor
Tokenizer *tokenizer_create(void)
{
    Tokenizer *tokenizer = malloc(sizeof(Tokenizer));
    if (!tokenizer) {
        log_fatal("tokenizer_create: out of memory");
        return NULL;
    }

    tokenizer->current_token = string_create_empty(0);
    if (!tokenizer->current_token) {
        free(tokenizer);
        log_fatal("tokenizer_create: failed to create current_token");
        return NULL;
    }

    tokenizer->tokens = token_array_create_with_free((TokenArrayFreeFunc)token_destroy);
    if (!tokenizer->tokens) {
        string_destroy(tokenizer->current_token);
        free(tokenizer);
        log_fatal("tokenizer_create: failed to create tokens array");
        return NULL;
    }

    tokenizer->heredoc_delim = NULL;
    tokenizer->heredoc_content = NULL;
    tokenizer->in_quotes = 0;
    tokenizer->in_dquotes = 0;
    tokenizer->escaped = 0;
    tokenizer->is_first_word = 1;
    tokenizer->after_heredoc_op = 0;
    tokenizer->paren_depth_dparen = 0;
    tokenizer->paren_depth_arith = 0;
    tokenizer->in_backtick = 0;
    tokenizer->brace_depth_param = 0;

    return tokenizer;
}

// Destructor
void tokenizer_destroy(Tokenizer *tokenizer)
{
    if (tokenizer) {
        log_debug("tokenizer_destroy: freeing tokenizer %p, tokens %zu",
                  tokenizer, token_array_size(tokenizer->tokens));
        string_destroy(tokenizer->heredoc_content);
        string_destroy(tokenizer->heredoc_delim);
        token_array_destroy(tokenizer->tokens);
        string_destroy(tokenizer->current_token);
        free(tokenizer);
    }
}

// Clear state and tokens
int tokenizer_clear(Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, -1);

    log_debug("tokenizer_clear: clearing tokenizer %p, tokens %zu",
              tokenizer, token_array_size(tokenizer->tokens));

    String *new_current = string_create_empty(0);
    if (!new_current) {
        log_fatal("tokenizer_clear: failed to create current_token");
        return -1;
    }
    string_destroy(tokenizer->current_token);
    tokenizer->current_token = new_current;

    if (token_array_clear(tokenizer->tokens) != 0) {
        log_fatal("tokenizer_clear: failed to clear tokens");
        return -1;
    }

    string_destroy(tokenizer->heredoc_delim);
    tokenizer->heredoc_delim = NULL;
    string_destroy(tokenizer->heredoc_content);
    tokenizer->heredoc_content = NULL;

    tokenizer->in_quotes = 0;
    tokenizer->in_dquotes = 0;
    tokenizer->escaped = 0;
    tokenizer->is_first_word = 1;
    tokenizer->after_heredoc_op = 0;
    tokenizer->paren_depth_dparen = 0;
    tokenizer->paren_depth_arith = 0;
    tokenizer->in_backtick = 0;
    tokenizer->brace_depth_param = 0;

    return 0;
}

// Token management
int tokenizer_add_token(Tokenizer *tokenizer, Token *token)
{
    return_val_if_null(tokenizer, -1);
    return_val_if_null(token, -1);

    if (token_array_append(tokenizer->tokens, token) != 0) {
        log_fatal("tokenizer_add_token: failed to append token");
        token_destroy(token);
        return -1;
    }

    return 0;
}

int tokenizer_add_token_cstr(Tokenizer *tokenizer, TokenType type, const char *text)
{
    return_val_if_null(tokenizer, -1);

    Token *token = token_create_from_cstr(type, text);
    if (!token) {
        log_fatal("tokenizer_add_token_cstr: failed to create token");
        return -1;
    }

    if (token_array_append(tokenizer->tokens, token) != 0) {
        log_fatal("tokenizer_add_token_cstr: failed to append token");
        token_destroy(token);
        return -1;
    }

    return 0;
}

const TokenArray *tokenizer_get_tokens(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, NULL);
    return tokenizer->tokens;
}

size_t tokenizer_token_count(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return token_array_size(tokenizer->tokens);
}

Token *tokenizer_get_token(const Tokenizer *tokenizer, size_t i)
{
    return_val_if_null (tokenizer, NULL);
    return_val_if_ge (i, tokenizer_token_count(tokenizer), NULL);
    return token_array_get (tokenizer_get_tokens (tokenizer), i);
}

// Parsing functions
int tokenizer_process_char(Tokenizer *tokenizer, char c)
{
    return_val_if_null(tokenizer, -1);

    String *current = tokenizer_get_current_token(tokenizer);
    if (!current) {
        log_fatal("tokenizer_process_char: no current token");
        return -1;
    }

    if (tokenizer_get_escaped(tokenizer)) {
        if (!tokenizer_get_in_quotes(tokenizer)) {
            if (string_append_ascii_char(current, c) != 0) {
                log_fatal("tokenizer_process_char: failed to append escaped char");
                return -1;
            }
        } else {
            if (c == '\'' || c == '\\') {
                if (string_append_ascii_char(current, c) != 0) {
                    log_fatal("tokenizer_process_char: failed to append escaped quote char");
                    return -1;
                }
            } else {
                if (string_append_ascii_char(current, '\\') != 0 ||
                    string_append_ascii_char(current, c) != 0) {
                    log_fatal("tokenizer_process_char: failed to append unescaped char");
                    return -1;
                }
            }
        }
        tokenizer_set_escaped(tokenizer, 0);
        return 0;
    }

    if (tokenizer_get_heredoc_delim(tokenizer)) {
        if (!tokenizer_get_heredoc_content(tokenizer)) {
            if (tokenizer_set_heredoc_content_cstr(tokenizer, "") != 0) {
                log_fatal("tokenizer_process_char: failed to initialize heredoc_content");
                return -1;
            }
        }
        if (string_append_ascii_char(tokenizer_get_heredoc_content(tokenizer), c) != 0) {
            log_fatal("tokenizer_process_char: failed to append heredoc content");
            return -1;
        }
        if (c == '\n') {
            const char *content = string_data(tokenizer_get_heredoc_content(tokenizer));
            const char *delim = string_data(tokenizer_get_heredoc_delim(tokenizer));
            size_t content_len = strlen(content);
            size_t delim_len = strlen(delim);
            if (content_len >= delim_len + 1 &&
                strcmp(content + content_len - delim_len - 1, delim) == 0 &&
                content[content_len - delim_len - 1] == '\n') {
                String *content_copy = string_create_from(tokenizer_get_heredoc_content(tokenizer));
                if (!content_copy) {
                    log_fatal("tokenizer_process_char: failed to copy heredoc content");
                    return -1;
                }
                if (tokenizer_add_token_cstr(tokenizer, TOKEN_WORD, string_data(content_copy)) != 0) {
                    log_fatal("tokenizer_process_char: failed to add heredoc content token");
                    string_destroy(content_copy);
                    return -1;
                }
                string_destroy(content_copy);
                if (tokenizer_set_heredoc_content(tokenizer, NULL) != 0 ||
                    tokenizer_set_heredoc_delim(tokenizer, NULL) != 0) {
                    log_fatal("tokenizer_process_char: failed to clear heredoc state");
                    return -1;
                }
            }
        }
        return 0;
    }

    if (tokenizer_get_after_heredoc_op(tokenizer)) {
        if (c == '\n') {
            const char str[2] = {c, 0};
            const char **pstr = (const char **)&str;
            if (!string_is_empty(current)) {
                if (process_heredoc(tokenizer, pstr) != 0) {
                    return -1;
                }
            }
            return 0;
        }
        if (string_append_ascii_char(current, c) != 0) {
            log_fatal("tokenizer_process_char: failed to append heredoc delim char");
            return -1;
        }
        return 0;
    }

    if (c == '\'' && !tokenizer_get_in_dquotes(tokenizer)) {
        tokenizer_set_in_quotes(tokenizer, !tokenizer_get_in_quotes(tokenizer));
        if (string_append_ascii_char(current, c) != 0) {
            log_fatal("tokenizer_process_char: failed to append '''");
            return -1;
        }
        return 0;
    }
    if (c == '"' && !tokenizer_get_in_quotes(tokenizer)) {
        tokenizer_set_in_dquotes(tokenizer, !tokenizer_get_in_dquotes(tokenizer));
        if (string_append_ascii_char(current, c) != 0) {
            log_fatal("tokenizer_process_char: failed to append '\"'");
            return -1;
        }
        return 0;
    }

    if (c == '\\' && !tokenizer_get_in_quotes(tokenizer)) {
        tokenizer_set_escaped(tokenizer, 1);
        return 0;
    }

    if (tokenizer_get_in_quotes(tokenizer) || tokenizer_get_in_dquotes(tokenizer)) {
        if (string_append_ascii_char(current, c) != 0) {
            log_fatal("tokenizer_process_char: failed to append quoted char");
            return -1;
        }
        return 0;
    }

    // Main parsing logic moved to tokenizer_process_input
    if (string_append_ascii_char(current, c) != 0) {
        log_fatal("tokenizer_process_char: failed to append char");
        return -1;
    }

    return 0;
}

int tokenizer_process_input(Tokenizer *tokenizer, const char *input)
{
    return_val_if_null(tokenizer, -1);
    return_val_if_null(input, -1);

    const char *p = input;

    while (*p) {
        if (tokenizer_get_heredoc_delim(tokenizer)) {
            if (process_heredoc_content(tokenizer, p) != 0) {
                return -1;
            }
            p += strlen(p); // Consume entire input as heredoc line
            break;
        }

        if (tokenizer_get_escaped(tokenizer)) {
            if (string_append_ascii_char(tokenizer_get_current_token(tokenizer), *p) != 0) {
                log_fatal("tokenizer_process_input: failed to append escaped char");
                return -1;
            }
            tokenizer_set_escaped(tokenizer, 0);
            p++;
            continue;
        }

        if (*p == '\\' && !tokenizer_get_in_quotes(tokenizer)) {
            if (*(p + 1) == '\0') {
                tokenizer_set_escaped(tokenizer, 1);
                return 0; // Line continuation
            }
            tokenizer_set_escaped(tokenizer, 1);
            p++;
            continue;
        }

        if (*p == '\'' && !tokenizer_get_in_dquotes(tokenizer)) {
            tokenizer_set_in_quotes(tokenizer, !tokenizer_get_in_quotes(tokenizer));
            if (string_append_ascii_char(tokenizer_get_current_token(tokenizer), *p) != 0) {
                log_fatal("tokenizer_process_input: failed to append '''");
                return -1;
            }
            p++;
            continue;
        }

        if (*p == '"' && !tokenizer_get_in_quotes(tokenizer)) {
            tokenizer_set_in_dquotes(tokenizer, !tokenizer_get_in_dquotes(tokenizer));
            if (string_append_ascii_char(tokenizer_get_current_token(tokenizer), *p) != 0) {
                log_fatal("tokenizer_process_input: failed to append '\"'");
                return -1;
            }
            p++;
            continue;
        }

        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer) && isspace(*p)) {
            if (!string_is_empty(tokenizer_get_current_token(tokenizer))) {
                if (tokenizer_get_after_heredoc_op(tokenizer)) {
                    if (process_heredoc(tokenizer, &p) != 0) {
                        return -1;
                    }
                } else {
                    if (finalize_current_token(tokenizer) != 0) {
                        return -1;
                    }
                }
            }
            if (*p == '\n') {
                if (tokenizer_add_token_cstr(tokenizer, TOKEN_NEWLINE, "\n") != 0) {
                    log_fatal("tokenizer_process_input: failed to add NEWLINE token");
                    return -1;
                }
                tokenizer_set_is_first_word(tokenizer, 1);
            }
            p++;
            continue;
        }

        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer) && *p == '#') {
            if (!string_is_empty(tokenizer_get_current_token(tokenizer))) {
                if (finalize_current_token(tokenizer) != 0) {
                    return -1;
                }
            }
            const char *start = p;
            while (*p && *p != '\n')
                p++;
            size_t len = p - start;
            char comment_str[1024];
            if (len >= sizeof(comment_str)) {
                log_fatal("tokenizer_process_input: comment too long");
                return -1;
            }
            strncpy(comment_str, start, len);
            comment_str[len] = '\0';
            if (tokenizer_add_token_cstr(tokenizer, TOKEN_COMMENT, comment_str) != 0) {
                log_fatal("tokenizer_process_input: failed to add COMMENT token");
                return -1;
            }
            continue;
        }

        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer) && strncmp(p, "$(", 2) == 0) {
            if (!string_is_empty(tokenizer_get_current_token(tokenizer))) {
                if (finalize_current_token(tokenizer) != 0) {
                    return -1;
                }
            }
            if (process_dparen(tokenizer, &p) != 0) {
                return -1;
            }
            continue;
        }

        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer) && strncmp(p, "$((", 3) == 0) {
            if (!string_is_empty(tokenizer_get_current_token(tokenizer))) {
                if (finalize_current_token(tokenizer) != 0) {
                    return -1;
                }
            }
            if (process_arith(tokenizer, &p) != 0) {
                return -1;
            }
            continue;
        }

        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer) && strncmp(p, "${", 2) == 0) {
            if (!string_is_empty(tokenizer_get_current_token(tokenizer))) {
                if (finalize_current_token(tokenizer) != 0) {
                    return -1;
                }
            }
            if (process_param(tokenizer, &p) != 0) {
                return -1;
            }
            continue;
        }

        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer) && *p == '`') {
            if (!string_is_empty(tokenizer_get_current_token(tokenizer))) {
                if (finalize_current_token(tokenizer) != 0) {
                    return -1;
                }
            }
            if (process_backtick(tokenizer, &p) != 0) {
                return -1;
            }
            continue;
        }

        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer) && is_operator_char(*p)) {
            if (!string_is_empty(tokenizer_get_current_token(tokenizer))) {
                if (finalize_current_token(tokenizer) != 0) {
                    return -1;
                }
            }
            if (process_operator(tokenizer, &p, tokenizer_get_current_token(tokenizer)) != 0) {
                return -1;
            }
            continue;
        }

        if (!tokenizer_get_in_quotes(tokenizer) && !tokenizer_get_in_dquotes(tokenizer) && *p == '~' &&
            string_is_empty(tokenizer_get_current_token(tokenizer))) {
            const char *start = p;
            p++;
            while (*p && (isalnum(*p) || *p == '_' || *p == '+' || *p == '-'))
                p++;
            size_t len = p - start;
            char tilde_str[256];
            if (len >= sizeof(tilde_str)) {
                log_fatal("tokenizer_process_input: tilde expansion too long");
                return -1;
            }
            strncpy(tilde_str, start, len);
            tilde_str[len] = '\0';
            if (tokenizer_add_token_cstr(tokenizer, TOKEN_TILDE, tilde_str) != 0) {
                log_fatal("tokenizer_process_input: failed to add TILDE token");
                return -1;
            }
            continue;
        }

        if (string_append_ascii_char(tokenizer_get_current_token(tokenizer), *p) != 0) {
            log_fatal("tokenizer_process_input: failed to append char");
            return -1;
        }
        p++;
    }

    return 0;
}

int tokenizer_process_string(Tokenizer *tokenizer, const String *input)
{
    if (!tokenizer || !input)
        return -1;
    return tokenizer_process_input(tokenizer, string_data(input));
}

int tokenizer_process_line(Tokenizer *tokenizer, const char *line) {
    if (tokenizer_get_after_heredoc_op(tokenizer)) {
        // Expecting delimiter
        tokenizer_set_heredoc_delim_cstr(tokenizer, line);
        tokenizer_set_after_heredoc_op(tokenizer, 0);
        tokenizer_set_heredoc_content_cstr(tokenizer, ""); // Start content collection
    } else if (tokenizer_get_heredoc_delim(tokenizer)) {
        // Collecting content
        if (strcmp(line, string_data(tokenizer_get_heredoc_delim(tokenizer))) == 0) {
            // Finalize here-document
            tokenizer_add_token_cstr(tokenizer, TOKEN_WORD, string_data(tokenizer_get_heredoc_content(tokenizer)));
            tokenizer_set_heredoc_delim(tokenizer, NULL);
            tokenizer_set_heredoc_content(tokenizer, NULL);
        } else {
            // Append to content
            // FIXME: should this be append_to_heredoc_content?
            process_heredoc_content(tokenizer, line);
        }
    } else {
        // Normal tokenization (e.g., parse operators like <<)
        if (strncmp(line, "<<", 2) == 0) {
            tokenizer_set_after_heredoc_op(tokenizer, 1);
            tokenizer_add_token_cstr(tokenizer, TOKEN_DLESS, "<<");
        } else {
            tokenizer_process_input(tokenizer, line);
        }
    }
    return 0;
}

bool tokenizer_is_complete(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, true);

    return (!(tokenizer_get_heredoc_delim(tokenizer) ||
              tokenizer_get_in_quotes(tokenizer) || tokenizer_get_in_dquotes(tokenizer) ||
              tokenizer_get_paren_depth_dparen(tokenizer) || tokenizer_get_paren_depth_arith(tokenizer) ||
              tokenizer_get_in_backtick(tokenizer) || tokenizer_get_brace_depth_param(tokenizer)));
}

int tokenizer_finalize(Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, -1);

    if (tokenizer_get_heredoc_delim(tokenizer)) {
        log_fatal("tokenizer_finalize: unclosed here-document");
        return -1;
    }
    if (tokenizer_get_in_quotes(tokenizer) || tokenizer_get_in_dquotes(tokenizer)) {
        log_fatal("tokenizer_finalize: unclosed quotes");
        return -1;
    }
    if (tokenizer_get_paren_depth_dparen(tokenizer) || tokenizer_get_paren_depth_arith(tokenizer) ||
        tokenizer_get_in_backtick(tokenizer) || tokenizer_get_brace_depth_param(tokenizer)) {
        log_fatal("tokenizer_finalize: unclosed substitution");
        return -1;
    }

    if (!string_is_empty(tokenizer_get_current_token(tokenizer))) {
        if (tokenizer_get_after_heredoc_op(tokenizer)) {
            const char *end = "";
            if (process_heredoc(tokenizer, &end) != 0) {
                return -1;
            }
        } else {
            if (finalize_current_token(tokenizer) != 0) {
                return -1;
            }
        }
    }

    // Post-process tokens for keywords, IO numbers, assignments
    size_t count = tokenizer_token_count(tokenizer);
    for (size_t i = 0; i < count; i++) {
        Token *token = (Token *)token_array_get(tokenizer_get_tokens(tokenizer), i);
        if (token_get_type(token) == TOKEN_WORD) {
            if (is_keyword_string(token_get_text(token))) {
                token_set_type(token, TOKEN_KEYWORD);
            } else if (i + 1 < count) {
                Token *next = (Token *)token_array_get(tokenizer_get_tokens(tokenizer), i + 1);
                const char *next_text = token_get_text_cstr(next);
                if ((token_get_type(next) == TOKEN_OPERATOR &&
                     (strcmp(next_text, ">") == 0 || strcmp(next_text, "<") == 0)) ||
                    token_get_type(next) == TOKEN_DLESS || token_get_type(next) == TOKEN_DGREAT ||
                    token_get_type(next) == TOKEN_DLESSDASH) {
                    if (is_number(token_get_text(token))) {
                        token_set_type(token, TOKEN_IO_NUMBER);
                    }
                }
            }
            const char *text = token_get_text_cstr(token);
            char *name = strdup(text);
            if (name) {
                char *eq = strchr(name, '=');
                if (eq && !strchr(name, ' ') && eq != name) {
                    *eq = '\0';
                    if (is_valid_name_cstr(name)) {
                        token_set_type(token, TOKEN_ASSIGNMENT);
                    }
                }
                free(name);
            }
        }
    }

    return 0;
}

// State getters
const String *tokenizer_get_current_token(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, NULL);
    return tokenizer->current_token;
}

const String *tokenizer_get_heredoc_delim(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, NULL);
    return tokenizer->heredoc_delim;
}

const String *tokenizer_get_heredoc_content(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, NULL);
    return tokenizer->heredoc_content;
}

int tokenizer_get_in_quotes(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return tokenizer->in_quotes;
}

int tokenizer_get_in_dquotes(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return tokenizer->in_dquotes;
}

int tokenizer_get_escaped(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return tokenizer->escaped;
}

int tokenizer_get_is_first_word(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return tokenizer->is_first_word;
}

int tokenizer_get_after_heredoc_op(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return tokenizer->after_heredoc_op;
}

int tokenizer_get_paren_depth_dparen(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return tokenizer->paren_depth_dparen;
}

int tokenizer_get_paren_depth_arith(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return tokenizer->paren_depth_arith;
}

int tokenizer_get_in_backtick(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return tokenizer->in_backtick;
}

int tokenizer_get_brace_depth_param(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, 0);
    return tokenizer->brace_depth_param;
}

// State setters
int tokenizer_set_current_token(Tokenizer *tokenizer, const String *current_token)
{
    return_val_if_null(tokenizer, -1);

    String *new_current = current_token ? string_create_from((String *)current_token) : string_create_empty(0);
    if (!new_current) {
        log_fatal("tokenizer_set_current_token: failed to create current_token");
        return -1;
    }

    string_destroy(tokenizer->current_token);
    tokenizer->current_token = new_current;
    return 0;
}

int tokenizer_set_current_token_cstr(Tokenizer *tokenizer, const char *current_token)
{
    return_val_if_null(tokenizer, -1);

    String *new_current = current_token ? string_create_from_cstr(current_token) : string_create_empty(0);
    if (!new_current) {
        log_fatal("tokenizer_set_current_token_cstr: failed to create current_token");
        return -1;
    }

    string_destroy(tokenizer->current_token);
    tokenizer->current_token = new_current;
    return 0;
}

int tokenizer_set_heredoc_delim(Tokenizer *tokenizer, const String *heredoc_delim)
{
    return_val_if_null(tokenizer, -1);

    String *new_delim = heredoc_delim ? string_create_from((String *)heredoc_delim) : NULL;
    if (heredoc_delim && !new_delim) {
        log_fatal("tokenizer_set_heredoc_delim: failed to create heredoc_delim");
        return -1;
    }

    string_destroy(tokenizer->heredoc_delim);
    tokenizer->heredoc_delim = new_delim;
    return 0;
}

int tokenizer_set_heredoc_delim_cstr(Tokenizer *tokenizer, const char *heredoc_delim)
{
    return_val_if_null(tokenizer, -1);

    String *new_delim = heredoc_delim ? string_create_from_cstr(heredoc_delim) : NULL;
    if (heredoc_delim && !new_delim) {
        log_fatal("tokenizer_set_heredoc_delim_cstr: failed to create heredoc_delim");
        return -1;
    }

    string_destroy(tokenizer->heredoc_delim);
    tokenizer->heredoc_delim = new_delim;
    return 0;
}

int tokenizer_set_heredoc_content(Tokenizer *tokenizer, const String *heredoc_content)
{
    return_val_if_null(tokenizer, -1);

    String *new_content = heredoc_content ? string_create_from((String *)heredoc_content) : NULL;
    if (heredoc_content && !new_content) {
        log_fatal("tokenizer_set_heredoc_content: failed to create heredoc_content");
        return -1;
    }

    string_destroy(tokenizer->heredoc_content);
    tokenizer->heredoc_content = new_content;
    return 0;
}

int tokenizer_set_heredoc_content_cstr(Tokenizer *tokenizer, const char *heredoc_content)
{
    return_val_if_null(tokenizer, -1);

    String *new_content = heredoc_content ? string_create_from_cstr(heredoc_content) : NULL;
    if (heredoc_content && !new_content) {
        log_fatal("tokenizer_set_heredoc_content_cstr: failed to create heredoc_content");
        return -1;
    }

    string_destroy(tokenizer->heredoc_content);
    tokenizer->heredoc_content = new_content;
    return 0;
}

int tokenizer_set_in_quotes(Tokenizer *tokenizer, int in_quotes)
{
    return_val_if_null(tokenizer, -1);
    tokenizer->in_quotes = in_quotes;
    return 0;
}

int tokenizer_set_in_dquotes(Tokenizer *tokenizer, int in_dquotes)
{
    return_val_if_null(tokenizer, -1);
    tokenizer->in_dquotes = in_dquotes;
    return 0;
}

int tokenizer_set_escaped(Tokenizer *tokenizer, int escaped)
{
    return_val_if_null(tokenizer, -1);
    tokenizer->escaped = escaped;
    return 0;
}

int tokenizer_set_is_first_word(Tokenizer *tokenizer, int is_first_word)
{
    return_val_if_null(tokenizer, -1);
    tokenizer->is_first_word = is_first_word;
    return 0;
}

int tokenizer_set_after_heredoc_op(Tokenizer *tokenizer, int after_heredoc_op)
{
    return_val_if_null(tokenizer, -1);
    tokenizer->after_heredoc_op = after_heredoc_op;
    return 0;
}

int tokenizer_set_paren_depth_dparen(Tokenizer *tokenizer, int paren_depth_dparen)
{
    return_val_if_null(tokenizer, -1);
    tokenizer->paren_depth_dparen = paren_depth_dparen;
    return 0;
}

int tokenizer_set_paren_depth_arith(Tokenizer *tokenizer, int paren_depth_arith)
{
    return_val_if_null(tokenizer, -1);
    tokenizer->paren_depth_arith = paren_depth_arith;
    return 0;
}

int tokenizer_set_in_backtick(Tokenizer *tokenizer, int in_backtick)
{
    return_val_if_null(tokenizer, -1);
    tokenizer->in_backtick = in_backtick;
    return 0;
}

int tokenizer_set_brace_depth_param(Tokenizer *tokenizer, int brace_depth_param)
{
    return_val_if_null(tokenizer, -1);
    tokenizer->brace_depth_param = brace_depth_param;
    return 0;
}

// Debugging
String *tokenizer_to_string(const Tokenizer *tokenizer)
{
    return_val_if_null(tokenizer, NULL);

    char buffer[512];
    const char *current_str = tokenizer->current_token ? string_data(tokenizer->current_token) : "(null)";
    const char *delim_str = tokenizer->heredoc_delim ? string_data(tokenizer->heredoc_delim) : "(null)";
    const char *content_str = tokenizer->heredoc_content ? string_data(tokenizer->heredoc_content) : "(null)";
    snprintf(buffer, sizeof(buffer),
             "[tokens=%zu, current='%s', heredoc_delim='%s', heredoc_content='%s', "
             "in_quotes=%d, in_dquotes=%d, escaped=%d, is_first_word=%d, "
             "after_heredoc_op=%d, paren_depth_dparen=%d, paren_depth_arith=%d, "
             "in_backtick=%d, brace_depth_param=%d]",
             token_array_size(tokenizer->tokens),
             current_str,
             delim_str,
             content_str,
             tokenizer->in_quotes,
             tokenizer->in_dquotes,
             tokenizer->escaped,
             tokenizer->is_first_word,
             tokenizer->after_heredoc_op,
             tokenizer->paren_depth_dparen,
             tokenizer->paren_depth_arith,
             tokenizer->in_backtick,
             tokenizer->brace_depth_param);

    String *result = string_create_from_cstr(buffer);
    if (!result) {
        log_fatal("tokenizer_to_string: failed to create string");
    }
    return result;
}
