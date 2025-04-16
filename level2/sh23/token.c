#include "token.h"
#include <stdlib.h>
#include <stdio.h>

struct Token {
    String *text;
    TokenType type;
    int quoted;
};

// Debugging: Convert TokenType to string
const char *token_type_to_string(TokenType type)
{
    switch (type) {
        case TOKEN_UNSPECIFIED: return "UNSPECIFIED";
        case TOKEN_WORD: return "WORD";
        case TOKEN_ASSIGNMENT: return "ASSIGNMENT";
        case TOKEN_NEWLINE: return "NEWLINE";
        case TOKEN_IO_NUMBER: return "IO_NUMBER";
        case TOKEN_OPERATOR: return "OPERATOR";
        case TOKEN_KEYWORD: return "KEYWORD";
        case TOKEN_PARAM: return "PARAM";
        case TOKEN_DPAREN: return "DPAREN";
        case TOKEN_BACKTICK: return "BACKTICK";
        case TOKEN_ARITH: return "ARITH";
        case TOKEN_TILDE: return "TILDE";
        case TOKEN_DLESS: return "DLESS";
        case TOKEN_DGREAT: return "DGREAT";
        case TOKEN_DLESSDASH: return "DLESSDASH";
        case TOKEN_LESSAND: return "LESSAND";
        case TOKEN_GREATAND: return "GREATAND";
        case TOKEN_LESSGREAT: return "LESSGREAT";
        case TOKEN_CLOBBER: return "CLOBBER";
        case TOKEN_HEREDOC_DELIM: return "HEREDOC_DELIM";
        case TOKEN_DSEMI: return "DSEMI";
        case TOKEN_SEMI: return "SEMI";
        case TOKEN_AMP: return "AMP";
        case TOKEN_AND_IF: return "AND_IF";
        case TOKEN_OR_IF: return "OR_IF";
        case TOKEN_COMMENT: return "COMMENT";
        case TOKEN_EOF: return "EOF";
        default: return "UNKNOWN";
    }
}

// Debugging: Convert Token to string representation
String *token_to_string(const Token *token)
{
    return_val_if_null(token, NULL);

    char buffer[256];
    const char *text_str = token->text ? string_data(token->text) : "(null)";
    snprintf(buffer, sizeof(buffer), "[type=%s, text='%s', quoted=%d]",
             token_type_to_string(token->type),
             text_str,
             token->quoted);

    String *result = string_create_from_cstr(buffer);
    if (!result) {
        log_fatal("token_to_string: failed to create string");
    }
    return result;
}

// Constructors
Token *token_create(TokenType type, const String *text)
{
    Token *token = malloc(sizeof(Token));
    if (!token) {
        log_fatal("token_create: out of memory");
        return NULL;
    }

    token->text = text ? string_create_from((String *)text) : NULL;
    if (text && !token->text) {
        free(token);
        log_fatal("token_create: failed to create text");
        return NULL;
    }

    token->type = type;
    token->quoted = 0; // Unused for now
    return token;
}

Token *token_create_from_cstr(TokenType type, const char *text)
{
    Token *token = malloc(sizeof(Token));
    if (!token) {
        log_fatal("token_create_from_cstr: out of memory");
        return NULL;
    }

    token->text = text ? string_create_from_cstr(text) : NULL;
    if (text && !token->text) {
        free(token);
        log_fatal("token_create_from_cstr: failed to create text");
        return NULL;
    }

    token->type = type;
    token->quoted = 0; // Unused for now
    return token;
}

// Destructor
void token_destroy(Token *token)
{
    if (token) {
        log_debug("token_destroy: freeing token %p, type = %d, text = %s, quoted = %d",
                  token,
                  token->type,
                  token->text ? string_data(token->text) : "(null)",
                  token->quoted);
        string_destroy(token->text);
        free(token);
    }
}

// Getters
TokenType token_get_type(const Token *token)
{
    return_val_if_null(token, TOKEN_UNSPECIFIED);
    return token->type;
}

const String *token_get_text(const Token *token)
{
    return_val_if_null(token, NULL);
    return token->text;
}

const char *token_get_text_cstr(const Token *token)
{
    return_val_if_null(token, NULL);
    return token->text ? string_data(token->text) : NULL;
}

int token_get_quoted(const Token *token)
{
    return_val_if_null(token, 0);
    return token->quoted;
}

// Setters
int token_set_type(Token *token, TokenType type)
{
    return_val_if_null(token, -1);
    token->type = type;
    return 0;
}

int token_set_text(Token *token, const String *text)
{
    return_val_if_null(token, -1);

    String *new_text = text ? string_create_from((String *)text) : NULL;
    if (text && !new_text) {
        log_fatal("token_set_text: failed to create text");
        return -1;
    }

    string_destroy(token->text);
    token->text = new_text;
    return 0;
}

int token_set_text_cstr(Token *token, const char *text)
{
    return_val_if_null(token, -1);

    String *new_text = text ? string_create_from_cstr(text) : NULL;
    if (text && !new_text) {
        log_fatal("token_set_text_cstr: failed to create text");
        return -1;
    }

    string_destroy(token->text);
    token->text = new_text;
    return 0;
}

int token_set_quoted(Token *token, int quoted)
{
    return_val_if_null(token, -1);
    token->quoted = quoted;
    return 0;
}
