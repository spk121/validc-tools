#include "tokenizer.h"
#include <stdlib.h>
#include <stdio.h>

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

// Constructor
Tokenizer *tokenizer_create(void)
{
    Tokenizer *tokenizer = malloc(sizeof(Tokenizer));
    if (!tokenizer) {
        log_fatal("tokenizer_create: out of memory");
        return NULL;
    }

    tokenizer->current_token = string_create_empty();
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

    String *new_current = string_create_empty();
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

    String *new_current = current_token ? string_create_from((String *)current_token) : string_create_empty();
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

    String *new_current = current_token ? string_create_from_cstr(current_token) : string_create_empty();
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
