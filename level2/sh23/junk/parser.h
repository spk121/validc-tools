#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include "variables.h"

#define MAX_TOKENS 1024
#define MAX_TOKEN_LEN 1024
#define MAX_COMMAND_LEN 4096
#define MAX_SHEBANG_LEN 256

// Token types for shell grammar
typedef enum {
    TOKEN, ASSIGNMENT_WORD, NAME, NEWLINE, IO_NUMBER,
    AND_IF, OR_IF, DSEMI, DLESS, DGREAT, LESSAND, GREATAND, LESSGREAT, DLESSDASH, CLOBBER,
    LESS, GREAT, PIPE, SEMI, AMP, LPAREN, RPAREN,
    IF, THEN, ELSE, ELIF, FI, DO, DONE, CASE, ESAC, WHILE, UNTIL, FOR,
    LBRACE, RBRACE, BANG, IN, WORD, TILDE
} TokenType;

typedef struct {
    char text[MAX_TOKEN_LEN];
    TokenType type;
} Token;

// AST node types
typedef enum {
    AST_SIMPLE_COMMAND, AST_PIPELINE, AST_AND_OR, AST_LIST, AST_COMPLETE_COMMAND, AST_PROGRAM,
    AST_IF_CLAUSE, AST_FOR_CLAUSE, AST_CASE_CLAUSE, AST_WHILE_CLAUSE, AST_UNTIL_CLAUSE,
    AST_BRACE_GROUP, AST_SUBSHELL, AST_FUNCTION_DEFINITION, AST_IO_REDIRECT,
    AST_EXPANSION
} ASTNodeType;

// Redirect operators
typedef enum {
    LESS, GREAT, DGREAT, DLESS, DLESSDASH, LESSAND, GREATAND, LESSGREAT, CLOBBER
} RedirectOperator;

// Redirect structure
typedef struct Redirect {
    char *io_number;
    RedirectOperator operator;
    char *filename;
    char *delimiter; // For heredocs
    char *heredoc_content; // Heredoc content
    int is_quoted; // Heredoc delimiter quoting
    int is_dash; // Heredoc <<- flag
    struct Redirect *next;
} Redirect;

// Case item for case clause
typedef struct CaseItem {
    char **patterns;
    int pattern_count;
    ASTNode *action;
    int has_dsemi; // Indicates ;;
    struct CaseItem *next;
} CaseItem;

// Expansion types
typedef enum {
    EXPANSION_PARAMETER, EXPANSION_SPECIAL, EXPANSION_DEFAULT, EXPANSION_ASSIGN,
    EXPANSION_SUBSTRING, EXPANSION_LENGTH, EXPANSION_PREFIX_SHORT, EXPANSION_PREFIX_LONG,
    EXPANSION_SUFFIX_SHORT, EXPANSION_SUFFIX_LONG, EXPANSION_COMMAND,
    EXPANSION_ARITHMETIC, EXPANSION_TILDE
} ExpansionType;

typedef struct {
    ExpansionType type;
    union {
        struct { // EXPANSION_PARAMETER
            char *name;
        } parameter;
        struct { // EXPANSION_SPECIAL
            char *name;
        } special;
        struct { // EXPANSION_DEFAULT, EXPANSION_ASSIGN
            char *var;
            char *default_value;
            int is_colon;
        } default_exp;
        struct { // EXPANSION_SUBSTRING
            char *var;
            char *offset;
            char *length;
        } substring;
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
            char **prefix;
            int prefix_count;
            char *command;
            char **suffix;
            int suffix_count;
            Expansion **expansions;
            int expansion_count;
            Redirect *redirects;
            int is_builtin;
            int break_levels; // For break/continue levels
        } simple_command;
        struct { // AST_PIPELINE
            ASTNode **commands;
            int command_count;
            int bang;
        } pipeline;
        struct { // AST_AND_OR
            ASTNode *left;
            ASTNode *right;
            int operator; // AND_IF, OR_IF
        } and_or;
        struct { // AST_LIST
            ASTNode *and_or;
            int separator; // SEMI, AMP
            ASTNode *next;
        } list;
        struct { // AST_COMPLETE_COMMAND
            ASTNode *list;
        } complete_command;
        struct { // AST_PROGRAM
            ASTNode *commands;
        } program;
        struct { // AST_IF_CLAUSE
            ASTNode *condition;
            ASTNode *then_body;
            ASTNode *else_part;
        } if_clause;
        struct { // AST_FOR_CLAUSE
            char *variable;
            char **wordlist;
            int wordlist_count;
            ASTNode *body;
        } for_clause;
        struct { // AST_CASE_CLAUSE
            char *word;
            CaseItem *items;
        } case_clause;
        struct { // AST_WHILE_CLAUSE
            ASTNode *condition;
            ASTNode *body;
        } while_clause;
        struct { // AST_UNTIL_CLAUSE
            ASTNode *condition;
            ASTNode *body;
        } until_clause;
        struct { // AST_BRACE_GROUP
            ASTNode *body;
        } brace_group;
        struct { // AST_SUBSHELL
            ASTNode *body;
        } subshell;
        struct { // AST_FUNCTION_DEFINITION
            char *name;
            ASTNode *body;
            Redirect *redirects;
        } function_definition;
        struct { // AST_IO_REDIRECT
            char *io_number;
            RedirectOperator operator;
            char *filename;
            char *delimiter;
            char *heredoc_content;
            int is_quoted;
            int is_dash;
        } io_redirect;
        struct { // AST_EXPANSION
            Expansion expansion;
        } expansion;
    } data;
} ASTNode;

// Execution status
typedef enum {
    EXEC_NORMAL, EXEC_BREAK, EXEC_CONTINUE, EXEC_RETURN
} ExecStatus;

// Function table for shell functions
typedef struct {
    char *name;
    ASTNode *body;
    Redirect *redirects;
    int active;
} Function;

typedef struct {
    Function *functions;
    int func_count;
    int func_capacity;
} FunctionTable;

// Built-in function type
typedef ExecStatus (*BuiltinFunc)(ASTNode *, Environment *, FunctionTable *, int *);

// Built-in definition
typedef struct {
    const char *name;
    BuiltinFunc func;
} Builtin;

// Parser state
typedef struct {
    Token *tokens;
    int token_count;
    int pos;
} ParserState;

typedef enum {
    PARSE_COMPLETE, PARSE_INCOMPLETE, PARSE_ERROR
} ParseStatus;

// Function prototypes
void init_parser_state(ParserState *state);
void free_parser_state(ParserState *state);
void tokenize(const char *input, Token *tokens, int *token_count, int depth,
              char **active_aliases, int active_alias_count);
ParseStatus parse_line(const char *input, ParserState *state, ASTNode **ast);
void print_ast(ASTNode *node, int depth);
void free_ast(ASTNode *node);
ExecStatus execute_ast(ASTNode *node, Environment *env, FunctionTable *ft, int *last_exit_status);
ExecStatus execute_simple_command(ASTNode *node, Environment *env, FunctionTable *ft, int *last_exit_status);
char *expand_parameter(Expansion *exp, Environment *env, FunctionTable *ft, int *last_exit_status);
char *expand_assignment(const char *assignment, Environment *env, FunctionTable *ft, int *last_exit_status);
void set_variable(Environment *env, const char *assignment);
const char *get_variable(Environment *env, const char *name);

#endif
