#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>

#define MAX_TOKENS 1024
#define MAX_TOKEN_LEN 1024
#define MAX_COMMAND_LEN 4096
#define MAX_SHEBANG_LEN 256

typedef enum {
    TOKEN, ASSIGNMENT_WORD, NAME, NEWLINE, IO_NUMBER,
    AND_IF, OR_IF, DSEMI, DLESS, DGREAT, LESSAND, GREATAND, LESSGREAT, DLESSDASH, CLOBBER,
    LESS, GREAT, PIPE, SEMI, AMP, LPAREN, RPAREN,
    IF, THEN, ELSE, ELIF, FI, DO, DONE, CASE, ESAC, WHILE, UNTIL, FOR,
    LBRACE, RBRACE, BANG, IN, WORD
} TokenType;

typedef struct {
    char text[MAX_TOKEN_LEN];
    TokenType type;
} Token;

typedef enum {
    AST_SIMPLE_COMMAND, AST_PIPELINE, AST_AND_OR, AST_LIST, AST_COMPLETE_COMMAND, AST_PROGRAM,
    AST_IF_CLAUSE, AST_FOR_CLAUSE, AST_CASE_CLAUSE, AST_WHILE_CLAUSE, AST_UNTIL_CLAUSE,
    AST_BRACE_GROUP, AST_SUBSHELL, AST_FUNCTION_DEFINITION, AST_IO_REDIRECT
} ASTNodeType;

typedef struct CaseItem {
    char **patterns;
    int pattern_count;
    struct ASTNode *action;
    int has_dsemi;
    struct CaseItem *next;
} CaseItem;

typedef struct Redirect {
    TokenType operator;
    char *filename;
    char *io_number;
    struct Redirect *next;
} Redirect;

typedef struct ASTNode {
    ASTNodeType type;
    union {
        struct { // AST_SIMPLE_COMMAND
            char **prefix;
            int prefix_count;
            char *command;
            char **suffix;
            int suffix_count;
            Redirect *redirects;
        } simple_command;
        struct { // AST_PIPELINE
            struct ASTNode **commands;
            int command_count;
            int bang;
        } pipeline;
        struct { // AST_AND_OR
            struct ASTNode *left;
            TokenType operator;
            struct ASTNode *right;
        } and_or;
        struct { // AST_LIST
            struct ASTNode *and_or;
            TokenType separator;
            struct ASTNode *next;
        } list;
        struct { // AST_COMPLETE_COMMAND
            struct ASTNode *list;
            TokenType separator;
        } complete_command;
        struct { // AST_PROGRAM
            struct ASTNode *commands;
        } program;
        struct { // AST_IF_CLAUSE
            struct ASTNode *condition;
            struct ASTNode *then_body;
            struct ASTNode *else_part;
        } if_clause;
        struct { // AST_FOR_CLAUSE
            char *variable;
            char **wordlist;
            int wordlist_count;
            struct ASTNode *body;
        } for_clause;
        struct { // AST_CASE_CLAUSE
            char *word;
            CaseItem *items;
        } case_clause;
        struct { // AST_WHILE_CLAUSE
            struct ASTNode *condition;
            struct ASTNode *body;
        } while_clause;
        struct { // AST_UNTIL_CLAUSE
            struct ASTNode *condition;
            struct ASTNode *body;
        } until_clause;
        struct { // AST_BRACE_GROUP
            struct ASTNode *body;
        } brace_group;
        struct { // AST_SUBSHELL
            struct ASTNode *body;
        } subshell;
        struct { // AST_FUNCTION_DEFINITION
            char *name;
            struct ASTNode *body;
            Redirect *redirects;
        } function_definition;
        struct { // AST_IO_REDIRECT
            TokenType operator;
            char *filename;
            char *io_number;
        } io_redirect;
    } data;
} ASTNode;

typedef enum {
    EXEC_NORMAL = 0,   // Normal execution
    EXEC_RETURN = 1    // Return signaled (function or script)
} ExecStatus;

typedef struct {
    char *name;
    ASTNode *body;
    Redirect *redirects;
    int active;        // New: Track if function is executing
} FunctionEntry;

typedef enum {
    PARSE_COMPLETE,
    PARSE_INCOMPLETE,
    PARSE_ERROR
} ParseStatus;

typedef struct {
    Token *tokens;
    int token_count;
    int token_capacity;
    int pos;
    int brace_depth;
    int paren_depth;
    int expecting;
    int in_function;   // New: Track if in function context
    int in_dot_script; // New: Track if in dot script context
} ParserState;

typedef struct {
    char *name;
    char *value;
    int exported; // 1 if exported, 0 otherwise
} Variable;

typedef struct {
    Variable *variables;
    int var_count;
    int var_capacity;
    char *shell_name; // For $0
    int arg_count;    // For $# (argc - 1)
    char **args;      // For $1-$9 (argv[1] and up)
} Environment;

typedef struct Function {
    char *name;
    ASTNode *body;
    Redirect *redirects;
    bool active;
} Function;

typedef struct {
    Function *functions;
    int func_count;
    int func_capacity;
} FunctionTable;

void init_parser_state(ParserState *state);
void free_parser_state(ParserState *state);
void init_environment(Environment *env, const char *shell_name, int argc, char *argv[]);
void free_environment(Environment *env);
void init_function_table(FunctionTable *ft);
void free_function_table(FunctionTable *ft);
void set_variable(Environment *env, const char *assignment);
const char *get_variable(Environment *env, const char *name);
void add_function(FunctionTable *ft, const char *name, ASTNode *body, Redirect *redirects);
ASTNode *get_function_body(FunctionTable *ft, const char *name);
Redirect *get_function_redirects(FunctionTable *ft, const char *name);
void tokenize(const char *input, Token *tokens, int *token_count);
ParseStatus parse_line(const char *line, ParserState *state, ASTNode **ast);
ExecStatus execute_ast(ASTNode *node, Environment *env, FunctionTable *ft, int *last_exit_status);
ExecStatus execute_simple_command(ASTNode *node, Environment *env, FunctionTable *ft, int *last_exit_status);
void free_ast(ASTNode *node);
void print_ast(ASTNode *node, int depth);
char *get_shebang_interpreter(const char *filename);
char *expand_assignment(const char *assignment, Environment *env, FunctionTable *ft, int *last_exit_status);
void export_variable(Environment *env, const char *name);
void unset_variable(Environment *env, const char *name);

#define EXPECTING_RBRACE  (1 << 0)
#define EXPECTING_RPAREN  (1 << 1)
#define EXPECTING_FI      (1 << 2)
#define EXPECTING_DONE    (1 << 3)
#define EXPECTING_ESAC    (1 << 4)

#endif
