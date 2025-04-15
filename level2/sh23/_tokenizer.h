#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "alias.h"
#include "string.h"
#include "ptr_array.h"

typedef enum
{
    TOKEN_UNSPECIFIED = 0, 
    TOKEN_WORD,          // Command, argument, variable, or here-document content
    TOKEN_ASSIGNMENT,    // Variable assignment (e.g., var=value)
    TOKEN_NEWLINE,       // \n
    TOKEN_IO_NUMBER,     // Number before redirection (e.g., 2>)
    TOKEN_OPERATOR,      // |, (,  ),  {,  }, etc.
    TOKEN_KEYWORD,       // Reserved words: if, while, for, etc.
    TOKEN_PARAM,         // Parameter expansion ${...} (e.g., "${var}", "${var:-word}", "${var-word}", "${var:=word}", "${var=word}", "${var:?word}", "${var?word}", "${var:+word}", "${var+word}", "${#parameter}", "${parameter%word}", "${parameter%%word}", "${parameter#word}", "${parameter##word}")
    TOKEN_DPAREN,        // Command substitution $(...) including inner content
    TOKEN_BACKTICK,      // Command substitution `...` including inner content
    TOKEN_ARITH,         // Arithmetic expansion $((...)) including inner content
    TOKEN_TILDE,         // ~ or ~user
    TOKEN_DLESS,         // << (heredoc operator, followed by TOKEN_HEREDOC_DELIM and TOKEN_WORD)
    TOKEN_DGREAT,        // >>
    TOKEN_DLESSDASH,     // <<- (heredoc with tab stripping, followed by TOKEN_HEREDOC_DELIM and TOKEN_WORD)
    TOKEN_LESSAND,       // <&
    TOKEN_GREATAND,      // >&
    TOKEN_LESSGREAT,     // <>
    TOKEN_CLOBBER,       // >|
    TOKEN_HEREDOC_DELIM, // Here-document delimiter (e.g., "EOF", "'EOF'")
    TOKEN_DSEMI,         // ;;
    TOKEN_SEMI,          // ;
    TOKEN_AMP,           // &
    TOKEN_AND_IF,        // &&
    TOKEN_OR_IF,         // ||
    TOKEN_COMMENT,       // Comment starting with # until newline (e.g., "# text")
    TOKEN_EOF,           // End of input
} TokenType;

typedef struct
{
    String *text;   // Token content (UTF-8); for TOKEN_WORD after TOKEN_HEREDOC_DELIM, contains content only
    TokenType type; // Token type
    int quoted;     // Unused for now, but could be use to indicate that the surrounding quote
                    // have been stripped off.
} Token;

// Tokenizer status for interactive mode
typedef enum
{
    TOKENIZER_SUCCESS,           // Line fully tokenized
    TOKENIZER_FAILURE,           // Tokenization error (e.g., memory allocation)
    TOKENIZER_LINE_CONTINUATION, // Backslash at end, awaiting more input
    TOKENIZER_HEREDOC            // Awaiting here-document delimiter
} TokenizerStatus;

// Tokenizer state for interactive mode
typedef struct
{
    String *current_token;   // Partial token being built
    PtrArray *tokens;        // Accumulated tokens
    String *heredoc_delim;   // Current here-document delimiter, if any
    String *heredoc_content; // Here-document content being collected
    int in_quotes;           // Single quote state
    int in_dquotes;          // Double quote state
    int escaped;             // Backslash escape state
    int is_first_word;       // For alias substitution
    int after_heredoc_op;    // Awaiting here-document delimiter
    int paren_depth_dparen;  // For $(...)
    int paren_depth_arith;   // For $((...)
    int in_backtick;         // For `...`
    int brace_depth_param;   // For ${...}
} Tokenizer;

// Create and destroy tokenizer
Tokenizer *tokenizer_create(void);
void tokenizer_destroy(Tokenizer *tokenizer);

// Free function for Token
void token_free(void *element);

// Create a new token
Token *token_create(String *text, TokenType type);

// Validate a name (alphanumeric or underscore, not starting with digit)
int is_valid_name_zstring(const char *name);
int is_valid_name_string(const String *name);

// Tokenize input into a PtrArray of Token*
TokenizerStatus tokenize_zstring(Tokenizer *tokenizer, const char *input, PtrArray *tokens, AliasStore *alias_store, int (*get_line)(char *buf, int size));
TokenizerStatus tokenize_string(Tokenizer *tokenizer, String *input, PtrArray *tokens, AliasStore *alias_store, int (*get_line)(char *buf, int size));
void token_print(Token *t);
#endif
