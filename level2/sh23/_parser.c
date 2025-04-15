#include "_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_ARRAY_CAPACITY 8
#define MAX_ERROR_MSG 256

// Helper: Allocate memory with error checking
static void *safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    return ptr;
}

// Helper: Duplicate string with error checking
static char *safe_strdup(const char *s) {
    if (!s) return NULL;
    char *dup = safe_malloc(strlen(s) + 1);
    strcpy(dup, s);
    return dup;
}

// Helper: Set error message
static void set_error(Parser *p, const char *fmt, ...) {
    if (!p->error_msg) {
        p->error_msg = safe_malloc(MAX_ERROR_MSG);
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(p->error_msg, MAX_ERROR_MSG, fmt, args);
    va_end(args);
}

// Helper: Create AST node
static ASTNode *ast_node_create(ASTNodeType type) {
    ASTNode *node = safe_malloc(sizeof(ASTNode));
    node->type = type;
    node->next = NULL;
    memset(&node->data, 0, sizeof(node->data));
    return node;
}

// Helper: Free string array
static void free_string_array(char **arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; i++) {
        free(arr[i]);
    }
    free(arr);
}

// Helper: Free expansion array
static void free_expansion_array(Expansion **arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; i++) {
        if (arr[i]) {
            Expansion *exp = arr[i];
            switch (exp->type) {
                case EXPANSION_PARAMETER:
                    free(exp->data.parameter.name);
                    break;
                case EXPANSION_SPECIAL:
                    break;
                case EXPANSION_DEFAULT:
                case EXPANSION_ASSIGN:
                case EXPANSION_ERROR_IF_UNSET:
                case EXPANSION_ALTERNATIVE:
                    free(exp->data.default_exp.var);
                    free(exp->data.default_exp.default_value);
                    break;
                case EXPANSION_LENGTH:
                    free(exp->data.length.var);
                    break;
                case EXPANSION_PREFIX_SHORT:
                case EXPANSION_PREFIX_LONG:
                case EXPANSION_SUFFIX_SHORT:
                case EXPANSION_SUFFIX_LONG:
                    free(exp->data.pattern.var);
                    free(exp->data.pattern.pattern);
                    break;
                case EXPANSION_COMMAND:
                    ast_free(exp->data.command.command);
                    break;
                case EXPANSION_ARITHMETIC:
                    free(exp->data.arithmetic.expression);
                    break;
                case EXPANSION_TILDE:
                    free(exp->data.tilde.user);
                    break;
            }
            free(exp);
        }
    }
    free(arr);
}

// Helper: Free redirect list
static void free_redirect_list(Redirect *redir) {
    while (redir) {
        Redirect *next = redir->next;
        free(redir->io_number);
        free(redir->filename);
        free(redir->delimiter);
        free(redir->heredoc_content);
        free(redir);
        redir = next;
    }
}

// Helper: Free case item list
static void free_case_item_list(CaseItem *item) {
    while (item) {
        CaseItem *next = item->next;
        free_string_array(item->patterns, item->pattern_count);
        ast_free(item->action);
        free(item);
        item = next;
    }
}

// Create parser
Parser *parser_create(Tokenizer *tokenizer, AliasStore *alias_store) {
    Parser *p = safe_malloc(sizeof(Parser));
    p->tokens = NULL;
    p->pos = 0;
    p->error_msg = NULL;
    p->line_number = 1;
    p->tokenizer = tokenizer; // Doesn't own, just references
    p->alias_store = alias_store;
    return p;
}

// Destroy parser
void parser_destroy(Parser *p) {
    if (!p) return;
    free(p->error_msg);
    free(p);
}

// Free AST
void ast_free(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_SIMPLE_COMMAND:
            for (int i = 0; i < node->data.simple_command.prefix_count; i++) {
                free(node->data.simple_command.prefix[i].name);
                free(node->data.simple_command.prefix[i].value);
            }
            free(node->data.simple_command.prefix);
            free(node->data.simple_command.command);
            free_string_array(node->data.simple_command.suffix, node->data.simple_command.suffix_count);
            free_expansion_array(node->data.simple_command.expansions, node->data.simple_command.expansion_count);
            free_redirect_list(node->data.simple_command.redirects);
            break;
        case AST_PIPELINE:
            for (int i = 0; i < node->data.pipeline.command_count; i++) {
                ast_free(node->data.pipeline.commands[i]);
            }
            free(node->data.pipeline.commands);
            break;
        case AST_AND_OR:
            ast_free(node->data.and_or.left);
            ast_free(node->data.and_or.right);
            break;
        case AST_LIST:
            ast_free(node->data.list.and_or);
            ast_free(node->data.list.next);
            break;
        case AST_COMPLETE_COMMAND:
            ast_free(node->data.complete_command.list);
            break;
        case AST_PROGRAM:
            ast_free(node->data.program.commands);
            break;
        case AST_IF_CLAUSE:
            ast_free(node->data.if_clause.condition);
            ast_free(node->data.if_clause.then_body);
            ast_free(node->data.if_clause.else_part);
            ast_free(node->data.if_clause.next);
            break;
        case AST_FOR_CLAUSE:
            free(node->data.for_clause.variable);
            free_string_array(node->data.for_clause.wordlist, node->data.for_clause.wordlist_count);
            ast_free(node->data.for_clause.body);
            ast_free(node->data.for_clause.next);
            break;
        case AST_CASE_CLAUSE:
            free(node->data.case_clause.word);
            free_case_item_list(node->data.case_clause.items);
            ast_free(node->data.case_clause.next);
            break;
        case AST_WHILE_CLAUSE:
            ast_free(node->data.while_clause.condition);
            ast_free(node->data.while_clause.body);
            ast_free(node->data.while_clause.next);
            break;
        case AST_UNTIL_CLAUSE:
            ast_free(node->data.until_clause.condition);
            ast_free(node->data.until_clause.body);
            ast_free(node->data.until_clause.next);
            break;
        case AST_BRACE_GROUP:
            ast_free(node->data.brace_group.body);
            ast_free(node->data.brace_group.next);
            break;
        case AST_SUBSHELL:
            ast_free(node->data.subshell.body);
            ast_free(node->data.subshell.next);
            break;
        case AST_FUNCTION_DEFINITION:
            free(node->data.function_definition.name);
            ast_free(node->data.function_definition.body);
            ast_free(node->data.function_definition.next);
            break;
        case AST_IO_REDIRECT:
            free(node->data.io_redirect.redirect.io_number);
            free(node->data.io_redirect.redirect.filename);
            free(node->data.io_redirect.redirect.delimiter);
            free(node->data.io_redirect.redirect.heredoc_content);
            break;
        case AST_EXPANSION:
            switch (node->data.expansion.expansion.type) {
                case EXPANSION_PARAMETER:
                    free(node->data.expansion.expansion.data.parameter.name);
                    break;
                case EXPANSION_SPECIAL:
                    break;
                case EXPANSION_DEFAULT:
                case EXPANSION_ASSIGN:
                case EXPANSION_ERROR_IF_UNSET:
                case EXPANSION_ALTERNATIVE:
                    free(node->data.expansion.expansion.data.default_exp.var);
                    free(node->data.expansion.expansion.data.default_exp.default_value);
                    break;
                case EXPANSION_LENGTH:
                    free(node->data.expansion.expansion.data.length.var);
                    break;
                case EXPANSION_PREFIX_SHORT:
                case EXPANSION_PREFIX_LONG:
                case EXPANSION_SUFFIX_SHORT:
                case EXPANSION_SUFFIX_LONG:
                    free(node->data.expansion.expansion.data.pattern.var);
                    free(node->data.expansion.expansion.data.pattern.pattern);
                    break;
                case EXPANSION_COMMAND:
                    ast_free(node->data.expansion.expansion.data.command.command);
                    break;
                case EXPANSION_ARITHMETIC:
                    free(node->data.expansion.expansion.data.arithmetic.expression);
                    break;
                case EXPANSION_TILDE:
                    free(node->data.expansion.expansion.data.tilde.user);
                    break;
            }
            break;
    }
    ast_free(node->next);
    free(node);
}

// Helper: Peek at current token
static Token *peek_token(Parser *p) {
    if (!p->tokens || p->pos >= ptr_array_size(p->tokens)) return NULL;
    return ptr_array_get(p->tokens, p->pos);
}

// Helper: Consume token
static Token *consume_token(Parser *p) {
    Token *token = peek_token(p);
    if (token) {
        if (token->type == TOKEN_NEWLINE) {
            p->line_number++;
        }
        p->pos++;
    }
    return token;
}

// Helper: Expect and consume token type
static int expect_token(Parser *p, TokenType type, const char *expected) {
    Token *token = peek_token(p);
    if (!token || token->type != type) {
        set_error(p, "Expected %s at line %d", expected, p->line_number);
        return 0;
    }
    if (type == TOKEN_OPERATOR || type == TOKEN_KEYWORD) {
        if (strcmp(string_data(token->text), expected) != 0) {
            set_error(p, "Expected '%s' at line %d, got '%s'", expected, p->line_number, string_data(token->text));
            return 0;
        }
    }
    consume_token(p);
    return 1;
}

// Helper: Parse redirect operation
static int parse_redirect_operation(Token *token, RedirectOperation *operation) {
    if (!token || (token->type != TOKEN_OPERATOR && token->type != TOKEN_DLESS &&
                   token->type != TOKEN_DGREAT && token->type != TOKEN_DLESSDASH &&
                   token->type != TOKEN_LESSAND && token->type != TOKEN_GREATAND &&
                   token->type != TOKEN_LESSGREAT && token->type != TOKEN_CLOBBER)) {
        return 0;
    }
    const char *text = string_data(token->text);
    if (strcmp(text, "<") == 0) *operation = LESS;
    else if (strcmp(text, ">") == 0) *operation = GREAT;
    else if (token->type == TOKEN_DGREAT) *operation = DGREAT;
    else if (token->type == TOKEN_DLESS) *operation = DLESS;
    else if (token->type == TOKEN_DLESSDASH) *operation = DLESSDASH;
    else if (token->type == TOKEN_LESSAND) *operation = LESSAND;
    else if (token->type == TOKEN_GREATAND) *operation = GREATAND;
    else if (token->type == TOKEN_LESSGREAT) *operation = LESSGREAT;
    else if (token->type == TOKEN_CLOBBER) *operation = CLOBBER;
    else return 0;
    return 1;
}

// Forward declarations
static ASTNode *parse_complete_command(Parser *p);
static ASTNode *parse_list(Parser *p);
static ASTNode *parse_and_or(Parser *p);
static ASTNode *parse_pipeline(Parser *p);
static ASTNode *parse_command(Parser *p);
static ASTNode *parse_simple_command(Parser *p);
static ASTNode *parse_compound_command(Parser *p);
static ASTNode *parse_function_definition(Parser *p);
static ASTNode *parse_expansion(Parser *p, Token *token);
static Redirect *parse_redirect_list(Parser *p);
static ASTNode *parse_compound_list(Parser *p);

// Parse expansion
static ASTNode *parse_expansion(Parser *p, Token *token) {
    if (!token || !p->tokenizer || !p->alias_store) return NULL;
    ASTNode *node = ast_node_create(AST_EXPANSION);
    Expansion *exp = &node->data.expansion.expansion;

    if (token->type == TOKEN_PARAM) {
        const char *text = string_data(token->text);
        if (text[0] != '$' || text[1] != '{') {
            set_error(p, "Invalid parameter expansion at line %d", p->line_number);
            ast_free(node);
            return NULL;
        }
        text += 2; // Skip ${

        // Check for length expansion
        if (*text == '#') {
            text++;
            exp->type = EXPANSION_LENGTH;
            exp->data.length.var = safe_strdup(text);
            if (text[strlen(text) - 1] != '}') {
                set_error(p, "Unclosed ${#...} at line %d", p->line_number);
                free(exp->data.length.var);
                ast_free(node);
                return NULL;
            }
            exp->data.length.var[strlen(exp->data.length.var) - 1] = '\0';
        }
        // Check for special parameters
        else if (strchr("*@#?!--0", *text) && (text[1] == '}' || isdigit(*text))) {
            exp->type = EXPANSION_SPECIAL;
            switch (*text) {
                case '*': exp->data.special.param = SPECIAL_STAR; break;
                case '@': exp->data.special.param = SPECIAL_AT; break;
                case '#': exp->data.special.param = SPECIAL_HASH; break;
                case '?': exp->data.special.param = SPECIAL_QUESTION; break;
                case '!': exp->data.special.param = SPECIAL_BANG; break;
                case '-': exp->data.special.param = SPECIAL_DASH; break;
                case '$': exp->data.special.param = SPECIAL_DOLLAR; break;
                case '0': exp->data.special.param = SPECIAL_ZERO; break;
                default:
                    if (isdigit(*text)) {
                        exp->type = EXPANSION_PARAMETER;
                        exp->data.parameter.name = safe_strdup(text);
                        exp->data.parameter.name[1] = '\0';
                    } else {
                        set_error(p, "Invalid special parameter at line %d", p->line_number);
                        ast_free(node);
                        return NULL;
                    }
            }
            if (exp->type == EXPANSION_SPECIAL && text[1] != '}') {
                set_error(p, "Invalid special parameter syntax at line %d", p->line_number);
                ast_free(node);
                return NULL;
            }
        }
        // Check for parameter expansions with operations
        else {
            char var[256] = {0};
            char *word = NULL;
            int is_colon = 0;
            const char *op = NULL;

            // Extract variable name
            int i = 0;
            while (*text && *text != ':' && *text != '-' && *text != '=' &&
                   *text != '?' && *text != '+' && *text != '%' && *text != '#' && *text != '}') {
                if (i < sizeof(var) - 1) var[i++] = *text;
                text++;
            }
            var[i] = '\0';

            if (*text == ':') {
                is_colon = 1;
                text++;
            }
            if (*text == '-' || *text == '=' || *text == '?' || *text == '+' ||
                *text == '%' || *text == '#') {
                op = text;
                text++;
                if (*text == *op && (*op == '%' || *op == '#')) {
                    text++; // Handle %% or ##
                }
                word = safe_strdup(text);
                if (word[strlen(word) - 1] == '}') {
                    word[strlen(word) - 1] = '\0';
                }
            } else if (*text != '}') {
                set_error(p, "Invalid parameter expansion syntax at line %d", p->line_number);
                ast_free(node);
                return NULL;
            }

            if (op) {
                if (*op == '-') {
                    exp->type = EXPANSION_DEFAULT;
                    exp->data.default_exp.var = safe_strdup(var);
                    exp->data.default_exp.default_value = word;
                    exp->data.default_exp.is_colon = is_colon;
                } else if (*op == '=') {
                    exp->type = EXPANSION_ASSIGN;
                    exp->data.default_exp.var = safe_strdup(var);
                    exp->data.default_exp.default_value = word;
                    exp->data.default_exp.is_colon = is_colon;
                } else if (*op == '?') {
                    exp->type = EXPANSION_ERROR_IF_UNSET;
                    exp->data.default_exp.var = safe_strdup(var);
                    exp->data.default_exp.default_value = word;
                    exp->data.default_exp.is_colon = is_colon;
                } else if (*op == '+') {
                    exp->type = EXPANSION_ALTERNATIVE;
                    exp->data.default_exp.var = safe_strdup(var);
                    exp->data.default_exp.default_value = word;
                    exp->data.default_exp.is_colon = is_colon;
                } else if (*op == '%' || *op == '#') {
                    exp->type = (*op == '%') ? (op[1] == '%' ? EXPANSION_SUFFIX_LONG : EXPANSION_SUFFIX_SHORT)
                                            : (op[1] == '#' ? EXPANSION_PREFIX_LONG : EXPANSION_PREFIX_SHORT);
                    exp->data.pattern.var = safe_strdup(var);
                    exp->data.pattern.pattern = word;
                }
            } else {
                exp->type = EXPANSION_PARAMETER;
                exp->data.parameter.name = safe_strdup(var);
            }
        }
    } else if (token->type == TOKEN_DPAREN || token->type == TOKEN_BACKTICK) {
        exp->type = EXPANSION_COMMAND;
        const char *text = string_data(token->text);
        int len = strlen(text);
        char *content = NULL;

        // Extract content (strip $(...) or `...`)
        if (token->type == TOKEN_DPAREN) {
            if (text[0] != '$' || text[1] != '(' || text[len - 1] != ')') {
                set_error(p, "Invalid command substitution syntax at line %d", p->line_number);
                ast_free(node);
                return NULL;
            }
            content = safe_malloc(len - 2);
            strncpy(content, text + 2, len - 3);
            content[len - 3] = '\0';
        } else {
            if (text[0] != '`' || text[len - 1] != '`') {
                set_error(p, "Invalid backtick syntax at line %d", p->line_number);
                ast_free(node);
                return NULL;
            }
            content = safe_malloc(len - 1);
            strncpy(content, text + 1, len - 2);
            content[len - 2] = '\0';
        }

        // Re-tokenize content
        PtrArray *sub_tokens = ptr_array_create_with_free(token_free);
        int line_number_save = p->line_number;
        TokenizerStatus t_status = tokenize_zstring(p->tokenizer, content, sub_tokens, p->alias_store, NULL);
        free(content);

        if (t_status != TOKENIZER_SUCCESS) {
            set_error(p, "Failed to tokenize command substitution at line %d", p->line_number);
            ptr_array_destroy(sub_tokens);
            ast_free(node);
            p->line_number = line_number_save;
            return NULL;
        }

        // Parse tokens into AST
        Parser *sub_parser = parser_create(p->tokenizer, p->alias_store);
        sub_parser->line_number = p->line_number;
        ASTNode *sub_ast = NULL;
        ParseStatus p_status = parser_apply_tokens(sub_parser, sub_tokens, &sub_ast);
        ptr_array_destroy(sub_tokens);

        if (p_status != PARSE_COMPLETE || !sub_ast) {
            set_error(p, "Failed to parse command substitution at line %d: %s",
                      p->line_number, sub_parser->error_msg ? sub_parser->error_msg : "Unknown error");
            ast_free(sub_ast);
            parser_destroy(sub_parser);
            p->line_number = line_number_save;
            ast_free(node);
            return NULL;
        }

        exp->data.command.command = sub_ast;
        parser_destroy(sub_parser);
        p->line_number = line_number_save;
    } else if (token->type == TOKEN_ARITH) {
        exp->type = EXPANSION_ARITHMETIC;
        exp->data.arithmetic.expression = safe_strdup(string_data(token->text));
    } else if (token->type == TOKEN_TILDE) {
        exp->type = EXPANSION_TILDE;
        exp->data.tilde.user = safe_strdup(string_data(token->text) + 1); // Skip ~
    } else {
        set_error(p, "Unexpected token in expansion at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }

    return node;
}

// Parse redirect
static Redirect *parse_redirect(Parser *p) {
    Token *io_number = NULL;
    Token *op_token = peek_token(p);
    RedirectOperation operation;

    // Check for IO number
    if (peek_token(p) && peek_token(p)->type == TOKEN_IO_NUMBER) {
        io_number = consume_token(p);
        op_token = peek_token(p);
    }

    if (!op_token || !parse_redirect_operation(op_token, &operation)) {
        if (io_number) {
            p->pos--;
            p->line_number--;
        }
        return NULL;
    }

    consume_token(p); // Consume operation
    Token *word = peek_token(p);
    if (!word || (word->type != TOKEN_WORD && word->type != TOKEN_HEREDOC_DELIM)) {
        set_error(p, "Expected filename or heredoc delimiter after redirect at line %d", p->line_number);
        p->pos--; // Backtrack
        if (io_number) {
            p->pos--;
            p->line_number--;
        }
        return NULL;
    }
    consume_token(p);

    Redirect *redir = safe_malloc(sizeof(Redirect));
    redir->io_number = io_number ? safe_strdup(string_data(io_number->text)) : NULL;
    redir->operation = operation;
    redir->filename = NULL;
    redir->delimiter = NULL;
    redir->heredoc_content = NULL;
    redir->is_quoted = 0;
    redir->is_dash = (operation == DLESSDASH);
    redir->next = NULL;

    if (operation == DLESS || operation == DLESSDASH) {
        redir->delimiter = safe_strdup(string_data(word->text));
        // Here-document content follows as TOKEN_WORD
        Token *content = peek_token(p);
        if (content && content->type == TOKEN_WORD) {
            redir->heredoc_content = safe_strdup(string_data(content->text));
            // Count newlines in heredoc content
            for (const char *c = redir->heredoc_content; *c; c++) {
                if (*c == '\n') p->line_number++;
            }
            consume_token(p);
        }
        // Check if delimiter was quoted (simplified check)
        redir->is_quoted = (strchr(redir->delimiter, '\'') || strchr(redir->delimiter, '"'));
    } else {
        redir->filename = safe_strdup(string_data(word->text));
    }

    return redir;
}

// Parse redirect list
static Redirect *parse_redirect_list(Parser *p) {
    Redirect *head = NULL, *tail = NULL;
    while (1) {
        Redirect *redir = parse_redirect(p);
        if (!redir) break;
        if (!head) {
            head = tail = redir;
        } else {
            tail->next = redir;
            tail = redir;
        }
    }
    return head;
}

// Parse simple command
// Parse simple command
static ASTNode *parse_simple_command(Parser *p) {
    ASTNode *node = ast_node_create(AST_SIMPLE_COMMAND);
    
    node->data.simple_command.prefix = NULL;
    node->data.simple_command.prefix_count = 0;
    node->data.simple_command.command = NULL;
    node->data.simple_command.suffix = NULL;
    node->data.simple_command.suffix_count = 0;
    node->data.simple_command.expansions = NULL;
    node->data.simple_command.expansion_count = 0;
    node->data.simple_command.redirects = NULL;
    node->data.simple_command.is_builtin = 0;
    node->data.simple_command.break_count = 0;
    node->data.simple_command.continue_count = 0;

    // Parse prefix (assignments) and redirects
    while (1) {
        Token *token = peek_token(p);
        if (!token) break;

        if (token->type == TOKEN_ASSIGNMENT) {
            consume_token(p);
            char *text = safe_strdup(string_data(token->text));
            char *eq = strchr(text, '=');
            if (!eq) {
                set_error(p, "Invalid assignment at line %d", p->line_number);
                free(text);
                ast_free(node);
                return NULL;
            }
            *eq = '\0';
            node->data.simple_command.prefix = realloc(node->data.simple_command.prefix, 
                sizeof(*node->data.simple_command.prefix) * (node->data.simple_command.prefix_count + 1));
            node->data.simple_command.prefix[node->data.simple_command.prefix_count].name = safe_strdup(text);
            node->data.simple_command.prefix[node->data.simple_command.prefix_count].value = safe_strdup(eq + 1);
            node->data.simple_command.prefix_count++;
            free(text);
        } else if (token->type == TOKEN_IO_NUMBER || token->type == TOKEN_OPERATOR ||
                   token->type == TOKEN_DLESS || token->type == TOKEN_DGREAT ||
                   token->type == TOKEN_DLESSDASH || token->type == TOKEN_LESSAND ||
                   token->type == TOKEN_GREATAND || token->type == TOKEN_LESSGREAT ||
                   token->type == TOKEN_CLOBBER) {
            Redirect *redir = parse_redirect(p);
            if (redir) {
                redir->next = node->data.simple_command.redirects;
                node->data.simple_command.redirects = redir;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    // Parse command word
    Token *token = peek_token(p);
    if (token && token->type == TOKEN_WORD) {
        consume_token(p);
        node->data.simple_command.command = safe_strdup(string_data(token->text));
        // Check for built-ins (simplified)
        if (strcmp(node->data.simple_command.command, "break") == 0 || 
            strcmp(node->data.simple_command.command, "continue") == 0) {
            node->data.simple_command.is_builtin = 1;
            Token *num = peek_token(p);
            if (num && num->type == TOKEN_WORD && is_number_zstring(string_data(num->text))) {
                consume_token(p);
                int count = atoi(string_data(num->text));
                if (strcmp(node->data.simple_command.command, "break") == 0) {
                    node->data.simple_command.break_count = count;
                } else {
                    node->data.simple_command.continue_count = count;
                }
            }
        }
    }

    // Parse suffix (arguments) and redirects
    while (1) {
        token = peek_token(p);
        if (!token) break;

        if (token->type == TOKEN_WORD) {
            consume_token(p);
            node->data.simple_command.suffix = realloc(node->data.simple_command.suffix, 
                sizeof(char *) * (node->data.simple_command.suffix_count + 1));
            node->data.simple_command.suffix[node->data.simple_command.suffix_count++] = 
                safe_strdup(string_data(token->text));
        } else if (token->type == TOKEN_IO_NUMBER || token->type == TOKEN_OPERATOR ||
                   token->type == TOKEN_DLESS || token->type == TOKEN_DGREAT ||
                   token->type == TOKEN_DLESSDASH || token->type == TOKEN_LESSAND ||
                   token->type == TOKEN_GREATAND || token->type == TOKEN_LESSGREAT ||
                   token->type == TOKEN_CLOBBER) {
            Redirect *redir = parse_redirect(p);
            if (redir) {
                redir->next = node->data.simple_command.redirects;
                node->data.simple_command.redirects = redir;
            } else {
                break;
            }
        } else if (token->type == TOKEN_PARAM || token->type == TOKEN_DPAREN ||
                   token->type == TOKEN_BACKTICK || token->type == TOKEN_ARITH ||
                   token->type == TOKEN_TILDE) {
            consume_token(p);
            ASTNode *exp_node = parse_expansion(p, token);
            if (exp_node) {
                node->data.simple_command.expansions = realloc(node->data.simple_command.expansions, 
                    sizeof(Expansion *) * (node->data.simple_command.expansion_count + 1));
                node->data.simple_command.expansions[node->data.simple_command.expansion_count++] = 
                    &exp_node->data.expansion.expansion;
                free(exp_node); // Expansion copied, free node
            } else {
                ast_free(node);
                return NULL;
            }
        } else {
            break;
        }
    }

    if (!node->data.simple_command.command && !node->data.simple_command.prefix_count && 
        !node->data.simple_command.redirects && !node->data.simple_command.expansion_count) {
        set_error(p, "Empty command at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }

    return node;
}

// Parse compound list
static ASTNode *parse_compound_list(Parser *p) {
    ASTNode *head = NULL, *tail = NULL;
    while (1) {
        ASTNode *and_or = parse_and_or(p);
        if (!and_or) break;

        ASTNode *list = ast_node_create(AST_LIST);
        list->data.list.and_or = and_or;
        list->data.list.separator = TOKEN_UNSPECIFIED;

        Token *sep = peek_token(p);
        if (sep && sep->type == TOKEN_OPERATOR) {
            if (strcmp(string_data(sep->text), ";") == 0) {
                list->data.list.separator = TOKEN_SEMI;
                consume_token(p);
            } else if (strcmp(string_data(sep->text), "&") == 0) {
                list->data.list.separator = TOKEN_AMP;
                consume_token(p);
            }
        }

        if (!head) {
            head = tail = list;
        } else {
            tail->data.list.next = list;
            tail = list;
        }

        if (list->data.list.separator != TOKEN_SEMI && list->data.list.separator != TOKEN_AMP) {
            break;
        }
    }
    return head;
}

// Parse brace group
static ASTNode *parse_brace_group(Parser *p) {
    if (!expect_token(p, TOKEN_OPERATOR, "{")) {
        return NULL;
    }
    ASTNode *node = ast_node_create(AST_BRACE_GROUP);
    node->data.brace_group.body = parse_compound_list(p);
    if (!node->data.brace_group.body || !expect_token(p, TOKEN_OPERATOR, "}")) {
        set_error(p, "Unclosed brace group at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    return node;
}

// Parse subshell
static ASTNode *parse_subshell(Parser *p) {
    if (!expect_token(p, TOKEN_OPERATOR, "(")) {
        return NULL;
    }
    ASTNode *node = ast_node_create(AST_SUBSHELL);
    node->data.subshell.body = parse_compound_list(p);
    if (!node->data.subshell.body || !expect_token(p, TOKEN_OPERATOR, ")")) {
        set_error(p, "Unclosed subshell at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    return node;
}

// Parse if clause
static ASTNode *parse_if_clause(Parser *p) {
    if (!expect_token(p, TOKEN_KEYWORD, "if")) {
        return NULL;
    }
    ASTNode *node = ast_node_create(AST_IF_CLAUSE);
    node->data.if_clause.condition = parse_compound_list(p);
    if (!node->data.if_clause.condition || !expect_token(p, TOKEN_KEYWORD, "then")) {
        set_error(p, "Invalid 'if' clause at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    node->data.if_clause.then_body = parse_compound_list(p);
    if (!node->data.if_clause.then_body) {
        set_error(p, "Missing 'then' body at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    Token *token = peek_token(p);
    if (token && token->type == TOKEN_KEYWORD && strcmp(string_data(token->text), "else") == 0) {
        consume_token(p);
        node->data.if_clause.else_part = parse_compound_list(p);
        if (!node->data.if_clause.else_part) {
            set_error(p, "Invalid 'else' part at line %d", p->line_number);
            ast_free(node);
            return NULL;
        }
    } else if (token && token->type == TOKEN_KEYWORD && strcmp(string_data(token->text), "elif") == 0) {
        node->data.if_clause.else_part = parse_if_clause(p);
        if (!node->data.if_clause.else_part) {
            ast_free(node);
            return NULL;
        }
    }
    if (!expect_token(p, TOKEN_KEYWORD, "fi")) {
        set_error(p, "Unclosed 'if' clause at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    return node;
}

// Parse for clause
static ASTNode *parse_for_clause(Parser *p) {
    if (!expect_token(p, TOKEN_KEYWORD, "for")) {
        return NULL;
    }
    Token *var = peek_token(p);
    if (!var || var->type != TOKEN_WORD || !is_valid_name_zstring(string_data(var->text))) {
        set_error(p, "Invalid variable name for 'for' at line %d", p->line_number);
        return NULL;
    }
    consume_token(p);
    ASTNode *node = ast_node_create(AST_FOR_CLAUSE);
    node->data.for_clause.variable = safe_strdup(string_data(var->text));

    // Check for 'in wordlist'
    Token *token = peek_token(p);
    if (token && token->type == TOKEN_KEYWORD && strcmp(string_data(token->text), "in") == 0) {
        consume_token(p);
        while (1) {
            token = peek_token(p);
            if (!token || token->type != TOKEN_WORD) break;
            consume_token(p);
            node->data.for_clause.wordlist = realloc(node->data.for_clause.wordlist,
                                                    sizeof(char *) * (node->data.for_clause.wordlist_count + 1));
            node->data.for_clause.wordlist[node->data.for_clause.wordlist_count++] =
                safe_strdup(string_data(token->text));
        }
        if (!expect_token(p, TOKEN_OPERATOR, ";")) {
            set_error(p, "Expected ';' after 'for' wordlist at line %d", p->line_number);
            ast_free(node);
            return NULL;
        }
    }

    // Parse 'do ... done'
    if (!expect_token(p, TOKEN_KEYWORD, "do")) {
        set_error(p, "Expected 'do' in 'for' clause at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    node->data.for_clause.body

 = parse_compound_list(p);
    if (!node->data.for_clause.body || !expect_token(p, TOKEN_KEYWORD, "done")) {
        set_error(p, "Unclosed 'for' clause at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    return node;
}

// Parse case clause
static ASTNode *parse_case_clause(Parser *p) {
    if (!expect_token(p, TOKEN_KEYWORD, "case")) {
        return NULL;
    }
    Token *word = peek_token(p);
    if (!word || word->type != TOKEN_WORD) {
        set_error(p, "Expected word after 'case' at line %d", p->line_number);
        return NULL;
    }
    consume_token(p);
    ASTNode *node = ast_node_create(AST_CASE_CLAUSE);
    node->data.case_clause.word = safe_strdup(string_data(word->text));

    if (!expect_token(p, TOKEN_KEYWORD, "in")) {
        set_error(p, "Expected 'in' after 'case' word at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }

    CaseItem **tail = &node->data.case_clause.items;
    while (1) {
        Token *token = peek_token(p);
        if (!token || (token->type == TOKEN_KEYWORD && strcmp(string_data(token->text), "esac") == 0)) {
            break;
        }
        CaseItem *item = safe_malloc(sizeof(CaseItem));
        item->patterns = NULL;
        item->pattern_count = 0;
        item->action = NULL;
        item->has_dsemi = 0;
        item->next = NULL;

        // Parse patterns
        if (!expect_token(p, TOKEN_OPERATOR, "(")) {
            set_error(p, "Expected '(' for case pattern at line %d", p->line_number);
            free(item);
            ast_free(node);
            return NULL;
        }
        while (1) {
            token = peek_token(p);
            if (!token || token->type != TOKEN_WORD) break;
            consume_token(p);
            item->patterns = realloc(item->patterns, sizeof(char *) * (item->pattern_count + 1));
            item->patterns[item->pattern_count++] = safe_strdup(string_data(token->text));
        }
        if (!expect_token(p, TOKEN_OPERATOR, ")")) {
            set_error(p, "Expected ')' after case patterns at line %d", p->line_number);
            free_case_item_list(item);
            ast_free(node);
            return NULL;
        }

        // Parse action
        item->action = parse_compound_list(p);
        token = peek_token(p);
        if (token && token->type == TOKEN_DSEMI) {
            item->has_dsemi = 1;
            consume_token(p);
        } else {
            set_error(p, "Expected ';;' after case item at line %d", p->line_number);
            free_case_item_list(item);
            ast_free(node);
            return NULL;
        }

        *tail = item;
        tail = &item->next;
    }

    if (!expect_token(p, TOKEN_KEYWORD, "esac")) {
        set_error(p, "Unclosed 'case' clause at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    return node;
}

// Parse while clause
static ASTNode *parse_while_clause(Parser *p) {
    if (!expect_token(p, TOKEN_KEYWORD, "while")) {
        return NULL;
    }
    ASTNode *node = ast_node_create(AST_WHILE_CLAUSE);
    node->data.while_clause.condition = parse_compound_list(p);
    if (!node->data.while_clause.condition || !expect_token(p, TOKEN_KEYWORD, "do")) {
        set_error(p, "Invalid 'while' clause at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    node->data.while_clause.body = parse_compound_list(p);
    if (!node->data.while_clause.body || !expect_token(p, TOKEN_KEYWORD, "done")) {
        set_error(p, "Unclosed 'while' clause at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    return node;
}

// Parse until clause
static ASTNode *parse_until_clause(Parser *p) {
    if (!expect_token(p, TOKEN_KEYWORD, "until")) {
        return NULL;
    }
    ASTNode *node = ast_node_create(AST_UNTIL_CLAUSE);
    node->data.until_clause.condition = parse_compound_list(p);
    if (!node->data.until_clause.condition || !expect_token(p, TOKEN_KEYWORD, "do")) {
        set_error(p, "Invalid 'until' clause at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    node->data.until_clause.body = parse_compound_list(p);
    if (!node->data.until_clause.body || !expect_token(p, TOKEN_KEYWORD, "done")) {
        set_error(p, "Unclosed 'until' clause at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    return node;
}

// Parse function definition
static ASTNode *parse_function_definition(Parser *p) {
    Token *name = peek_token(p);
    if (!name || name->type != TOKEN_WORD || !is_valid_name_zstring(string_data(name->text))) {
        return NULL;
    }
    consume_token(p);
    if (!expect_token(p, TOKEN_OPERATOR, "(") || !expect_token(p, TOKEN_OPERATOR, ")")) {
        set_error(p, "Invalid function definition syntax at line %d", p->line_number);
        p->pos -= 2; // Backtrack
        return NULL;
    }
    ASTNode *node = ast_node_create(AST_FUNCTION_DEFINITION);
    node->data.function_definition.name = safe_strdup(string_data(name->text));
    node->data.function_definition.body = parse_compound_command(p);
    if (!node->data.function_definition.body) {
        set_error(p, "Missing function body at line %d", p->line_number);
        ast_free(node);
        return NULL;
    }
    // Parse redirects (attached to body)
    Redirect *redirects = parse_redirect_list(p);
    if (redirects) {
        // Attach to body (simplified)
        if (node->data.function_definition.body->type == AST_BRACE_GROUP ||
            node->data.function_definition.body->type == AST_SUBSHELL) {
            node->data.function_definition.body->data.brace_group.body->data.simple_command.redirects = redirects;
        }
    }
    return node;
}

// Parse compound command
static ASTNode *parse_compound_command(Parser *p) {
    Token *token = peek_token(p);
    if (!token) return NULL;

    if (token->type == TOKEN_KEYWORD) {
        const char *kw = string_data(token->text);
        if (strcmp(kw, "if") == 0) return parse_if_clause(p);
        if (strcmp(kw, "for") == 0) return parse_for_clause(p);
        if (strcmp(kw, "case") == 0) return parse_case_clause(p);
        if (strcmp(kw, "while") == 0) return parse_while_clause(p);
        if (strcmp(kw, "until") == 0) return parse_until_clause(p);
    } else if (token->type == TOKEN_OPERATOR) {
        const char *op = string_data(token->text);
        if (strcmp(op, "{") == 0) return parse_brace_group(p);
        if (strcmp(op, "(") == 0) return parse_subshell(p);
    }
    return NULL;
}

// Parse command
static ASTNode *parse_command(Parser *p) {
    ASTNode *cmd = parse_compound_command(p);
    if (cmd) {
        cmd->next = parse_redirect_list(p); // Attach redirects
        return cmd;
    }
    cmd = parse_function_definition(p);
    if (cmd) return cmd;
    return parse_simple_command(p);
}

// Parse pipeline
static ASTNode *parse_pipeline(Parser *p) {
    int bang = 0;
    Token *token = peek_token(p);
    if (token && token->type == TOKEN_OPERATOR && strcmp(string_data(token->text), "!") == 0) {
        consume_token(p);
        bang = 1;
    }

    ASTNode *cmd = parse_command(p);
    if (!cmd) {
        if (bang) {
            p->pos--;
            p->line_number--;
        }
        return NULL;
    }

    ASTNode *node = ast_node_create(AST_PIPELINE);
    node->data.pipeline.bang = bang;
    node->data.pipeline.commands = safe_malloc(sizeof(ASTNode *) * INITIAL_ARRAY_CAPACITY);
    node->data.pipeline.commands[0] = cmd;
    node->data.pipeline.command_count = 1;

    while (1) {
        token = peek_token(p);
        if (!token || token->type != TOKEN_OPERATOR || strcmp(string_data(token->text), "|") != 0) {
            break;
        }
        consume_token(p);
        cmd = parse_command(p);
        if (!cmd) {
            set_error(p, "Incomplete pipeline after '|' at line %d", p->line_number);
            ast_free(node);
            return NULL;
        }
        if (node->data.pipeline.command_count >= INITIAL_ARRAY_CAPACITY) {
            node->data.pipeline.commands = realloc(node->data.pipeline.commands,
                                                  sizeof(ASTNode *) * (node->data.pipeline.command_count + 1));
        }
        node->data.pipeline.commands[node->data.pipeline.command_count++] = cmd;
    }

    return node;
}

// Parse and-or
static ASTNode *parse_and_or(Parser *p) {
    ASTNode *left = parse_pipeline(p);
    if (!left) return NULL;

    Token *token = peek_token(p);
    if (!token || (token->type != TOKEN_AND_IF && token->type != TOKEN_OR_IF)) {
        return left;
    }

    ASTNode *node = ast_node_create(AST_AND_OR);
    node->data.and_or.left = left;
    node->data.and_or.operation = token->type;
    consume_token(p);

    node->data.and_or.right = parse_and_or(p);
    if (!node->data.and_or.right) {
        set_error(p, "Incomplete and-or expression after '%s' at line %d",
                  token->type == TOKEN_AND_IF ? "&&" : "||", p->line_number);
        ast_free(node);
        return NULL;
    }

    return node;
}

// Parse list
static ASTNode *parse_list(Parser *p) {
    ASTNode *and_or = parse_and_or(p);
    if (!and_or) return NULL;

    ASTNode *node = ast_node_create(AST_LIST);
    node->data.list.and_or = and_or;
    node->data.list.separator = TOKEN_UNSPECIFIED;

    Token *token = peek_token(p);
    if (token && token->type == TOKEN_OPERATOR) {
        if (strcmp(string_data(token->text), ";") == 0) {
            node->data.list.separator = TOKEN_SEMI;
            consume_token(p);
        } else if (strcmp(string_data(token->text), "&") == 0) {
            node->data.list.separator = TOKEN_AMP;
            consume_token(p);
        }
    }

    token = peek_token(p);
    if (token && token->type != TOKEN_EOF &&
        !(token->type == TOKEN_KEYWORD && (strcmp(string_data(token->text), "fi") == 0 ||
                                          strcmp(string_data(token->text), "done") == 0 ||
                                          strcmp(string_data(token->text), "esac") == 0))) {
        node->data.list.next = parse_list(p);
    }

    return node;
}

// Parse complete command
static ASTNode *parse_complete_command(Parser *p) {
    ASTNode *list = parse_list(p);
    if (!list) return NULL;

    ASTNode *node = ast_node_create(AST_COMPLETE_COMMAND);
    node->data.complete_command.list = list;

    // Consume optional NEWLINEs
    while (peek_token(p) && peek_token(p)->type == TOKEN_NEWLINE) {
        consume_token(p);
    }

    return node;
}

// Parse program
ParseStatus parser_apply_tokens(Parser *p, const PtrArray *tokens, ASTNode **ast) {
    if (!p || !tokens || !ast || !p->tokenizer || !p->alias_store) {
        set_error(p, "Invalid parser state");
        return PARSE_ERROR;
    }

    p->tokens = (PtrArray *)tokens;
    p->pos = 0;
    free(p->error_msg);
    p->error_msg = NULL;
    *ast = NULL;

    Token *token = peek_token(p);
    if (!token || token->type == TOKEN_EOF) {
        return PARSE_COMPLETE;
    }

    ASTNode *program = ast_node_create(AST_PROGRAM);
    program->data.program.commands = parse_complete_command(p);

    if (!program->data.program.commands) {
        ast_free(program);
        if (p->pos < ptr_array_size(p->tokens)) {
            token = peek_token(p);
            if (token && token->type == TOKEN_KEYWORD && (strcmp(string_data(token->text), "fi") == 0 ||
                                                         strcmp(string_data(token->text), "done") == 0 ||
                                                         strcmp(string_data(token->text), "esac") == 0)) {
                set_error(p, "Incomplete construct, expected closing '%s' at line %d",
                          string_data(token->text), p->line_number);
                return PARSE_INCOMPLETE;
            }
            if (!p->error_msg) {
                set_error(p, "Syntax error at line %d", p->line_number);
            }
            return PARSE_ERROR;
        }
        set_error(p, "Incomplete input at line %d", p->line_number);
        return PARSE_INCOMPLETE;
    }

    token = peek_token(p);
    if (token && token->type != TOKEN_EOF) {
        // Check for incomplete constructs
        if (token->type == TOKEN_KEYWORD && (strcmp(string_data(token->text), "fi") == 0 ||
                                            strcmp(string_data(token->text), "done") == 0 ||
                                            strcmp(string_data(token->text), "esac") == 0)) {
            set_error(p, "Unclosed construct, found '%s' at line %d", string_data(token->text), p->line_number);
            ast_free(program);
            return PARSE_INCOMPLETE;
        }
        set_error(p, "Unexpected tokens after command at line %d", p->line_number);
        ast_free(program);
        return PARSE_ERROR;
    }

    *ast = program;
    return PARSE_COMPLETE;
}

// Print AST (for debugging)
void ast_print(ASTNode *node, int depth) {
    if (!node) return;
    for (int i = 0; i < depth; i++) printf("  ");
    switch (node->type) {
        case AST_SIMPLE_COMMAND:
            printf("SIMPLE_COMMAND: %s\n", node->data.simple_command.command ? node->data.simple_command.command : "");
            for (int i = 0; i < node->data.simple_command.prefix_count; i++) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("PREFIX: %s=%s\n", node->data.simple_command.prefix[i].name,
                       node->data.simple_command.prefix[i].value);
            }
            for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("SUFFIX: %s\n", node->data.simple_command.suffix[i]);
            }
            for (int i = 0; i < node->data.simple_command.expansion_count; i++) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("EXPANSION: type=%d\n", node->data.simple_command.expansions[i]->type);
                if (node->data.simple_command.expansions[i]->type == EXPANSION_COMMAND) {
                    ast_print(node->data.simple_command.expansions[i]->data.command.command, depth + 2);
                }
            }
            for (Redirect *r = node->data.simple_command.redirects; r; r = r->next) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("REDIRECT: operation=%d file=%s\n", r->operation, r->filename ? r->filename : r->delimiter);
            }
            break;
        case AST_PIPELINE:
            printf("PIPELINE: bang=%d\n", node->data.pipeline.bang);
            for (int i = 0; i < node->data.pipeline.command_count; i++) {
                ast_print(node->data.pipeline.commands[i], depth + 1);
            }
            break;
        case AST_AND_OR:
            printf("AND_OR: operation=");
            switch (node->data.and_or.operation) {
                case TOKEN_AND_IF: printf("&&"); break;
                case TOKEN_OR_IF: printf("||"); break;
                case TOKEN_UNSPECIFIED: printf("none"); break;
                default: printf("invalid"); break;
            }
            printf("\n");
            ast_print(node->data.and_or.left, depth + 1);
            ast_print(node->data.and_or.right, depth + 1);
            break;
        case AST_LIST:
            printf("LIST: sep=");
            switch (node->data.list.separator) {
                case TOKEN_SEMI: printf(";"); break;
                case TOKEN_AMP: printf("&"); break;
                case TOKEN_UNSPECIFIED: printf("none"); break;
                default: printf("invalid"); break;
            }
            printf("\n");
            ast_print(node->data.list.and_or, depth + 1);
            ast_print(node->data.list.next, depth + 1);
            break;
        case AST_COMPLETE_COMMAND:
            printf("COMPLETE_COMMAND\n");
            ast_print(node->data.complete_command.list, depth + 1);
            break;
        case AST_PROGRAM:
            printf("PROGRAM\n");
            ast_print(node->data.program.commands, depth + 1);
            break;
        case AST_IF_CLAUSE:
            printf("IF_CLAUSE\n");
            ast_print(node->data.if_clause.condition, depth + 1);
            ast_print(node->data.if_clause.then_body, depth + 1);
            ast_print(node->data.if_clause.else_part, depth + 1);
            ast_print(node->data.if_clause.next, depth + 1);
            break;
        case AST_FOR_CLAUSE:
            printf("FOR_CLAUSE: var=%s\n", node->data.for_clause.variable);
            ast_print(node->data.for_clause.body, depth + 1);
            ast_print(node->data.for_clause.next, depth + 1);
            break;
        case AST_CASE_CLAUSE:
            printf("CASE_CLAUSE: word=%s\n", node->data.case_clause.word);
            for (CaseItem *item = node->data.case_clause.items; item; item = item->next) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("CASE_ITEM\n");
                ast_print(item->action, depth + 2);
            }
            ast_print(node->data.case_clause.next, depth + 1);
            break;
        case AST_WHILE_CLAUSE:
            printf("WHILE_CLAUSE\n");
            ast_print(node->data.while_clause.condition, depth + 1);
            ast_print(node->data.while_clause.body, depth + 1);
            ast_print(node->data.while_clause.next, depth + 1);
            break;
        case AST_UNTIL_CLAUSE:
            printf("UNTIL_CLAUSE\n");
            ast_print(node->data.until_clause.condition, depth + 1);
            ast_print(node->data.until_clause.body, depth + 1);
            ast_print(node->data.until_clause.next, depth + 1);
            break;
        case AST_BRACE_GROUP:
            printf("BRACE_GROUP\n");
            ast_print(node->data.brace_group.body, depth + 1);
            ast_print(node->data.brace_group.next, depth + 1);
            break;
        case AST_SUBSHELL:
            printf("SUBSHELL\n");
            ast_print(node->data.subshell.body, depth + 1);
            ast_print(node->data.subshell.next, depth + 1);
            break;
        case AST_FUNCTION_DEFINITION:
            printf("FUNCTION_DEFINITION: name=%s\n", node->data.function_definition.name);
            ast_print(node->data.function_definition.body, depth + 1);
            ast_print(node->data.function_definition.next, depth + 1);
            break;
        case AST_IO_REDIRECT:
            printf("IO_REDIRECT: operation=%d\n", node->data.io_redirect.redirect.operation);
            break;
        case AST_EXPANSION:
            printf("EXPANSION: type=%d\n", node->data.expansion.expansion.type);
            break;
    }
    ast_print(node->next, depth);
}
