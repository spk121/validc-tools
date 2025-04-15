#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include "_tokenizer.h" // For Token, TokenType, Tokenizer

// AST node types
typedef enum {
    AST_SIMPLE_COMMAND,
    AST_PIPELINE,
    AST_AND_OR,
    AST_LIST,
    AST_COMPLETE_COMMAND,
    AST_PROGRAM,
    AST_IF_CLAUSE,
    AST_FOR_CLAUSE,
    AST_CASE_CLAUSE,
    AST_WHILE_CLAUSE,
    AST_UNTIL_CLAUSE,
    AST_BRACE_GROUP,
    AST_SUBSHELL,
    AST_FUNCTION_DEFINITION,
    AST_IO_REDIRECT,
    AST_EXPANSION
} ASTNodeType;

// Redirect operations
typedef enum {
    LESS,       // <
    GREAT,      // >
    DGREAT,     // >>
    DLESS,      // <<
    DLESSDASH,  // <<-
    LESSAND,    // <&
    GREATAND,   // >&
    LESSGREAT,  // <>
    CLOBBER     // >|
} RedirectOperation;

// Redirect structure
typedef struct Redirect {
    char *io_number;        // e.g., "2"
    RedirectOperation operation;
    char *filename;         // For <, >, etc.
    char *delimiter;        // For heredocs
    char *heredoc_content;  // Heredoc content
    int is_quoted;          // Heredoc delimiter quoting
    int is_dash;            // Heredoc <<- flag
    struct Redirect *next;
} Redirect;

// Case item for case clause
typedef struct CaseItem {
    char **patterns;
    int pattern_count;
    struct ASTNode *action;
    int has_dsemi;          // Indicates ;;
    struct CaseItem *next;
} CaseItem;

// Special parameter types
typedef enum {
    SPECIAL_STAR, SPECIAL_AT, SPECIAL_HASH, SPECIAL_QUESTION,
    SPECIAL_BANG, SPECIAL_DASH, SPECIAL_DOLLAR, SPECIAL_ZERO
} SpecialParameter;

// Expansion types
typedef enum {
    EXPANSION_PARAMETER,      // $1, $2, ..., $9
    EXPANSION_SPECIAL,        // $*, $@, $#, $?, $!, $-, $$, $0
    EXPANSION_DEFAULT,        // ${parameter:-[word]}, ${parameter-[word]}
    EXPANSION_ASSIGN,         // ${parameter:=[word]}, ${parameter=[word]}
    EXPANSION_ERROR_IF_UNSET, // ${parameter:?[word]}, ${parameter?[word]}
    EXPANSION_ALTERNATIVE,    // ${parameter:+[word]}, ${parameter+[word]}
    EXPANSION_LENGTH,         // ${#parameter}
    EXPANSION_PREFIX_SHORT,   // ${parameter#[word]}
    EXPANSION_PREFIX_LONG,    // ${parameter##[word]}
    EXPANSION_SUFFIX_SHORT,   // ${parameter%[word]}
    EXPANSION_SUFFIX_LONG,    // ${parameter%%[word]}
    EXPANSION_COMMAND,        // `...` or $(...)
    EXPANSION_ARITHMETIC,     // $(())
    EXPANSION_TILDE           // ~
} ExpansionType;

typedef struct {
    ExpansionType type;
    union {
        struct { // EXPANSION_PARAMETER
            char *name;
        } parameter;
        struct { // EXPANSION_SPECIAL
            SpecialParameter param;
        } special;
        struct { // EXPANSION_DEFAULT, EXPANSION_ASSIGN, EXPANSION_ERROR_IF_UNSET, EXPANSION_ALTERNATIVE
            char *var;
            char *default_value;
            int is_colon;
        } default_exp;
        struct { // EXPANSION_LENGTH
            char *var;
        } length;
        struct { // EXPANSION_PREFIX_SHORT, EXPANSION_PREFIX_LONG, EXPANSION_SUFFIX_SHORT, EXPANSION_SUFFIX_LONG
            char *var;
            char *pattern;
        } pattern;
        struct { // EXPANSION_COMMAND
            struct ASTNode *command;
        } command;
        struct { // EXPANSION_ARITHMETIC
            char *expression;
        } arithmetic;
        struct { // EXPANSION_TILDE
            char *user;
        } tilde;
    } data;
} Expansion;

// AST node structure
typedef struct ASTNode {
    ASTNodeType type;
    union {
        struct { // AST_SIMPLE_COMMAND
            struct { char *name; char *value; } *prefix; // Assignments (x=1)
            int prefix_count;
            char *command;
            char **suffix;
            int suffix_count;
            Expansion **expansions;
            int expansion_count;
            Redirect *redirects;
            int is_builtin;
            int break_count;
            int continue_count;
        } simple_command;
        struct { // AST_PIPELINE
            struct ASTNode **commands;
            int command_count;
            int bang;
        } pipeline;
        struct { // AST_AND_OR
            struct ASTNode *left;
            struct ASTNode *right;
            TokenType operation; // TOKEN_AND_IF, TOKEN_OR_IF, TOKEN_UNSPECIFIED
        } and_or;
        struct { // AST_LIST
            struct ASTNode *and_or;
            TokenType separator; // TOKEN_SEMI, TOKEN_AMP, TOKEN_UNSPECIFIED
            struct ASTNode *next;
        } list;
        struct { // AST_COMPLETE_COMMAND
            struct ASTNode *list;
        } complete_command;
        struct { // AST_PROGRAM
            struct ASTNode *commands;
        } program;
        struct { // AST_IF_CLAUSE
            struct ASTNode *condition;
            struct ASTNode *then_body;
            struct ASTNode *else_part;
            struct ASTNode *next;
        } if_clause;
        struct { // AST_FOR_CLAUSE
            char *variable;
            char **wordlist;
            int wordlist_count;
            struct ASTNode *body;
            struct ASTNode *next;
        } for_clause;
        struct { // AST_CASE_CLAUSE
            char *word;
            CaseItem *items;
            struct ASTNode *next;
        } case_clause;
        struct { // AST_WHILE_CLAUSE
            struct ASTNode *condition;
            struct ASTNode *body;
            struct ASTNode *next;
        } while_clause;
        struct { // AST_UNTIL_CLAUSE
            struct ASTNode *condition;
            struct ASTNode *body;
            struct ASTNode *next;
        } until_clause;
        struct { // AST_BRACE_GROUP
            struct ASTNode *body;
            struct ASTNode *next;
        } brace_group;
        struct { // AST_SUBSHELL
            struct ASTNode *body;
            struct ASTNode *next;
        } subshell;
        struct { // AST_FUNCTION_DEFINITION
            char *name;
            struct ASTNode *body; // Includes redirects
            struct ASTNode *next;
        } function_definition;
        struct { // AST_IO_REDIRECT
            Redirect redirect;
        } io_redirect;
        struct { // AST_EXPANSION
            Expansion expansion;
        } expansion;
    } data;
    struct ASTNode *next; // Generic next for chaining
} ASTNode;

// Parser state
typedef struct {
    PtrArray *tokens;
    int pos;
    char *error_msg;        // Error message for diagnostics
    int line_number;        // Current line number
    Tokenizer *tokenizer;   // For re-tokenizing command substitutions
    AliasStore *alias_store;// For alias expansion in command substitutions
} Parser;

typedef enum {
    PARSE_COMPLETE,
    PARSE_INCOMPLETE,
    PARSE_ERROR
} ParseStatus;

// Function prototypes
Parser *parser_create(Tokenizer *tokenizer, AliasStore *alias_store);
void parser_destroy(Parser *p);
ParseStatus parser_apply_tokens(Parser *p, const PtrArray *tokens, ASTNode **ast);
void ast_print(ASTNode *node, int depth);

#endif

