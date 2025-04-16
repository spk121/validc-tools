#ifndef TOKEN_H
#define TOKEN_H

#include <stdbool.h>
#include "string.h"
#include "logging.h"

typedef enum
{
    TOKEN_UNSPECIFIED = 0, 
    TOKEN_WORD,          // Command, argument, variable, or here-document content
    TOKEN_ASSIGNMENT,    // Variable assignment (e.g., var=value)
    TOKEN_NEWLINE,       // \n
    TOKEN_IO_NUMBER,     // Number before redirection (e.g., 2>)
    TOKEN_OPERATOR,      // |, (,  ),  {,  }, etc.
    TOKEN_KEYWORD,       // Reserved words: if, while, for, etc.
    TOKEN_PARAM,         // Parameter expansion ${...}
    TOKEN_DPAREN,        // Command substitution $(...)
    TOKEN_BACKTICK,      // Command substitution `...`
    TOKEN_ARITH,         // Arithmetic expansion $((...))
    TOKEN_TILDE,         // ~ or ~user
    TOKEN_DLESS,         // <<
    TOKEN_DGREAT,        // >>
    TOKEN_DLESSDASH,     // <<-
    TOKEN_LESSAND,       // <&
    TOKEN_GREATAND,      // >&
    TOKEN_LESSGREAT,     // <>
    TOKEN_CLOBBER,       // >|
    TOKEN_HEREDOC_DELIM, // Here-document delimiter (e.g., "EOF")
    TOKEN_DSEMI,         // ;;
    TOKEN_SEMI,          // ;
    TOKEN_AMP,           // &
    TOKEN_AND_IF,        // &&
    TOKEN_OR_IF,         // ||
    TOKEN_COMMENT,       // Comment starting with #
    TOKEN_EOF,           // End of input
} TokenType;

typedef struct Token Token;

// Constructors
Token *token_create(TokenType type, const String *text);
Token *token_create_from_cstr(TokenType type, const char *text);

// Destructor
void token_destroy(Token *token);

// Getters
TokenType token_get_type(const Token *token);
const String *token_get_text(const Token *token);
const char *token_get_text_cstr(const Token *token);
int token_get_quoted(const Token *token);

// Setters
int token_set_type(Token *token, TokenType type);
int token_set_text(Token *token, const String *text);
int token_set_text_cstr(Token *token, const char *text);
int token_set_quoted(Token *token, int quoted);

// Debugging
const char *token_type_to_string(TokenType type);
String *token_to_string(const Token *token);

#endif
