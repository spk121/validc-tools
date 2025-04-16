#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "string.h"
#include "token.h"
#include "token_array.h"
#include "logging.h"

typedef struct Tokenizer Tokenizer;

// Validate a name (POSIX: alphanumeric or underscore, not starting with digit)
int is_valid_name(const String *name);
int is_valid_name_cstr(const char *name);

// Validate a number (all digits)
int is_number(const String *text);
int is_number_cstr(const char *text);

// Constructor
Tokenizer *tokenizer_create(void);

// Destructor
void tokenizer_destroy(Tokenizer *tokenizer);

// Clear state and tokens
int tokenizer_clear(Tokenizer *tokenizer);

// Token management
int tokenizer_add_token(Tokenizer *tokenizer, Token *token);
int tokenizer_add_token_cstr(Tokenizer *tokenizer, TokenType type, const char *text);
const TokenArray *tokenizer_get_tokens(const Tokenizer *tokenizer);
size_t tokenizer_token_count(const Tokenizer *tokenizer);

// Parsing functions
int tokenizer_process_char(Tokenizer *tokenizer, char c);
int tokenizer_process_input(Tokenizer *tokenizer, const char *input);
int tokenizer_process_string(Tokenizer *tokenizer, const String *input);
int tokenizer_finalize(Tokenizer *tokenizer);

// State getters
const String *tokenizer_get_current_token(const Tokenizer *tokenizer);
const String *tokenizer_get_heredoc_delim(const Tokenizer *tokenizer);
const String *tokenizer_get_heredoc_content(const Tokenizer *tokenizer);
int tokenizer_get_in_quotes(const Tokenizer *tokenizer);
int tokenizer_get_in_dquotes(const Tokenizer *tokenizer);
int tokenizer_get_escaped(const Tokenizer *tokenizer);
int tokenizer_get_is_first_word(const Tokenizer *tokenizer);
int tokenizer_get_after_heredoc_op(const Tokenizer *tokenizer);
int tokenizer_get_paren_depth_dparen(const Tokenizer *tokenizer);
int tokenizer_get_paren_depth_arith(const Tokenizer *tokenizer);
int tokenizer_get_in_backtick(const Tokenizer *tokenizer);
int tokenizer_get_brace_depth_param(const Tokenizer *tokenizer);

// State setters
int tokenizer_set_current_token(Tokenizer *tokenizer, const String *current_token);
int tokenizer_set_current_token_cstr(Tokenizer *tokenizer, const char *current_token);
int tokenizer_set_heredoc_delim(Tokenizer *tokenizer, const String *heredoc_delim);
int tokenizer_set_heredoc_delim_cstr(Tokenizer *tokenizer, const char *heredoc_delim);
int tokenizer_set_heredoc_content(Tokenizer *tokenizer, const String *heredoc_content);
int tokenizer_set_heredoc_content_cstr(Tokenizer *tokenizer, const char *heredoc_content);
int tokenizer_set_in_quotes(Tokenizer *tokenizer, int in_quotes);
int tokenizer_set_in_dquotes(Tokenizer *tokenizer, int in_dquotes);
int tokenizer_set_escaped(Tokenizer *tokenizer, int escaped);
int tokenizer_set_is_first_word(Tokenizer *tokenizer, int is_first_word);
int tokenizer_set_after_heredoc_op(Tokenizer *tokenizer, int after_heredoc_op);
int tokenizer_set_paren_depth_dparen(Tokenizer *tokenizer, int paren_depth_dparen);
int tokenizer_set_paren_depth_arith(Tokenizer *tokenizer, int paren_depth_arith);
int tokenizer_set_in_backtick(Tokenizer *tokenizer, int in_backtick);
int tokenizer_set_brace_depth_param(Tokenizer *tokenizer, int brace_depth_param);

// Debugging
String *tokenizer_to_string(const Tokenizer *tokenizer);

#endif
