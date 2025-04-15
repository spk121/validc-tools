#ifndef TOKENIZER_H
#define TOKENIZER_H

#ifndef ZSTRING_DEFINE
#define ZSTRING_DEFINE
typedef char *zstring; ///< A null-terminated string
#endif

#define MAX_TOKEN_LEN 256
#define MAX_ALIAS_SUBSTITUTION_LEN 256
#define MAX_TOKENS 100
#define MAX_ALIAS_DEPTH 100
#define MAX_ALIASES_LEN 200
#define MAX_HEREDOC_CONTENT 1024
#define MAX_LINE_LEN 1024

typedef enum {
    TOKEN, WORD, NAME, ASSIGNMENT_WORD, IO_NUMBER, NEWLINE, OPERATOR,
    DO, LPAREN, DLESS, DLESSDASH
} TokenType;

typedef struct {
    char text[MAX_TOKEN_LEN];
    TokenType type;
} Token;

typedef struct {
    Token tokens[MAX_TOKENS];
    int count;
} TokenList;

typedef enum {
    TOKENIZE_COMPLETE,
    TOKENIZE_LINE_CONTINUATION,
    TOKENIZE_HEREDOC,
    TOKENIZE_ERROR
} TokenizeStatus;

typedef struct {
    int active;
    char delimiter[MAX_TOKEN_LEN];
    int quoted;
    int strip_tabs;
    char content[MAX_HEREDOC_CONTENT];
    int content_pos;
} HeredocState;

typedef struct {
    char name[MAX_TOKEN_LEN];
    char substitution[MAX_ALIAS_SUBSTITUTION_LEN];
} Alias;

typedef struct {
    Alias aliases[MAX_ALIASES_LEN];
    int count;
} AliasList;

typedef struct {
    char line[MAX_LINE_LEN];
    int pos;
    int len;
} InputLine;

typedef struct {
    TokenList tokens;
    AliasList aliases;
    HeredocState heredoc;
    InputLine input;
    bool in_word;
    int depth;
    TokenizeStatus status;
} Tokenizer;

void tokenizer_initialize (Tokenizer *t);
TokenizeStatus tokenizer_add_line (Tokenizer *t, const char *line);
const TokenList* tokenizer_get_tokens (Tokenizer *t);
void tokenizer_reset (Tokenizer *t);

#endif
