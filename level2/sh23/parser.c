#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "parser.h"
#include "builtins.h"

static const char *operators[] = {
    "&&", "||", ";;", "<<", ">>", "<&", ">&", "<>", "<<-", ">|", "<", ">", "|", ";", "&", "(", ")", NULL
};

static const char *reserved_words[] = {
    "if", "then", "else", "elif", "fi", "do", "done",
    "case", "esac", "while", "until", "for",
    "{", "}", "!", "in", NULL
};

static int is_operator_start(char c) {
    return (c == '&' || c == '|' || c == ';' || c == '(' || c == ')' || c == '<' || c == '>');
}

static TokenType get_operator_type(const char *str) {
    if (strcmp(str, "&&") == 0) return AND_IF;
    if (strcmp(str, "||") == 0) return OR_IF;
    if (strcmp(str, ";;") == 0) return DSEMI;
    if (strcmp(str, "<<") == 0) return DLESS;
    if (strcmp(str, ">>") == 0) return DGREAT;
    if (strcmp(str, "<&") == 0) return LESSAND;
    if (strcmp(str, ">&") == 0) return GREATAND;
    if (strcmp(str, "<>") == 0) return LESSGREAT;
    if (strcmp(str, "<<-") == 0) return DLESSDASH;
    if (strcmp(str, ">|") == 0) return CLOBBER;
    if (strcmp(str, "<") == 0) return LESS;
    if (strcmp(str, ">") == 0) return GREAT;
    if (strcmp(str, "|") == 0) return PIPE;
    if (strcmp(str, ";") == 0) return SEMI;
    if (strcmp(str, "&") == 0) return AMP;
    if (strcmp(str, "(") == 0) return LPAREN;
    if (strcmp(str, ")") == 0) return RPAREN;
    return TOKEN;
}

static int is_operator(const char *str) {
    return get_operator_type(str) != TOKEN;
}

static TokenType get_reserved_word_type(const char *str) {
    if (strcmp(str, "if") == 0) return IF;
    if (strcmp(str, "then") == 0) return THEN;
    if (strcmp(str, "else") == 0) return ELSE;
    if (strcmp(str, "elif") == 0) return ELIF;
    if (strcmp(str, "fi") == 0) return FI;
    if (strcmp(str, "do") == 0) return DO;
    if (strcmp(str, "done") == 0) return DONE;
    if (strcmp(str, "case") == 0) return CASE;
    if (strcmp(str, "esac") == 0) return ESAC;
    if (strcmp(str, "while") == 0) return WHILE;
    if (strcmp(str, "until") == 0) return UNTIL;
    if (strcmp(str, "for") == 0) return FOR;
    if (strcmp(str, "{") == 0) return LBRACE;
    if (strcmp(str, "}") == 0) return RBRACE;
    if (strcmp(str, "!") == 0) return BANG;
    if (strcmp(str, "in") == 0) return IN;
    return TOKEN;
}

static int is_all_digits(const char *str) {
    for (int i = 0; str[i]; i++) {
        if (!isdigit((unsigned char)str[i])) return 0;
    }
    return 1;
}

static int is_valid_name(const char *str) {
    if (!str[0] || (!isalpha((unsigned char)str[0]) && str[0] != '_')) return 0;
    for (int i = 1; str[i]; i++) {
        if (!isalnum((unsigned char)str[i]) && str[i] != '_') return 0;
    }
    return 1;
}

static const char *parse_expansion(const char *input, char *token, int *pos, int max_len, int in_double_quote) {
    const char *p = input;
    if (*p == '$') {
        token[(*pos)++] = *p++;
        if (*p == '{') {
            token[(*pos)++] = *p++;
            int brace_count = 1;
            while (*p && brace_count > 0) {
                if (*p == '{') brace_count++;
                else if (*p == '}') brace_count--;
                if (in_double_quote && *p == '\\' && p[1] && strchr("\"'", p[1])) {
                    token[(*pos)++] = *p++;
                    token[(*pos)++] = *p++;
                } else {
                    token[(*pos)++] = *p++;
                }
            }
        } else if (*p == '(') {
            token[(*pos)++] = *p++;
            int paren_count = 1;
            while (*p && paren_count > 0) {
                if (*p == '(') paren_count++;
                else if (*p == ')') paren_count--;
                if (in_double_quote && *p == '\\' && p[1] && strchr("\"'", p[1])) {
                    token[(*pos)++] = *p++;
                    token[(*pos)++] = *p++;
                } else if (*p == '$' || *p == '`') {
                    p = parse_expansion(p, token, pos, max_len, in_double_quote);
                } else {
                    token[(*pos)++] = *p++;
                }
            }
        } else {
            while (*p && (isalnum(*p) || *p == '_')) token[(*pos)++] = *p++;
        }
    } else if (*p == '`') {
        token[(*pos)++] = *p++;
        while (*p && *p != '`') {
            if (*p == '\\' && p[1]) {
                token[(*pos)++] = *p++;
                token[(*pos)++] = *p++;
            } else if (*p == '$' || *p == '`') {
                p = parse_expansion(p, token, pos, max_len, in_double_quote);
            } else {
                token[(*pos)++] = *p++;
            }
        }
        if (*p) token[(*pos)++] = *p++;
    }
    return p;
}

void tokenize(const char *input, Token *tokens, int *token_count) {
    *token_count = 0;
    const char *p = input;
    char current_token[MAX_TOKEN_LEN];
    int pos = 0;
    int in_word = 0;

    while (*p) {
        if (*p == '\0') {
            if (in_word) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                (*token_count)++;
            }
            break;
        }

        if (*p == '#' && !in_word) {
            while (*p && *p != '\n') p++;
            continue;
        }

        if (*p == '\n' && !in_word) {
            current_token[pos] = '\0';
            if (pos > 0) {
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                (*token_count)++;
            }
            strncpy(tokens[*token_count].text, "\n", MAX_TOKEN_LEN);
            tokens[*token_count].type = NEWLINE;
            (*token_count)++;
            pos = 0;
            p++;
            continue;
        }

        if (*p == '\\' || *p == '\'' || *p == '"') {
            if (!in_word) in_word = 1;
            if (pos >= MAX_TOKEN_LEN - 1) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
            }

            if (*p == '\\') {
                current_token[pos++] = *p++;
                if (*p && *p != '\n') {
                    current_token[pos++] = *p++;
                } else if (*p == '\n') {
                    p++;
                }
                continue;
            }

            if (*p == '\'') {
                current_token[pos++] = *p++;
                while (*p && *p != '\'') {
                    current_token[pos++] = *p++;
                }
                if (*p == '\'') {
                    current_token[pos++] = *p++;
                } else {
                    fprintf(stderr, "Error: Unmatched single quote\n");
                    return;
                }
                continue;
            }

            if (*p == '"') {
                current_token[pos++] = *p++;
                while (*p && *p != '"') {
                    if (*p == '\\') {
                        current_token[pos++] = *p++;
                        if (*p && strchr("$`\"\\", *p)) {
                            current_token[pos++] = *p++;
                        } else if (*p == '\n') {
                            p++;
                        } else if (*p) {
                            current_token[pos++] = *p++;
                        }
                    } else if (*p == '$' || *p == '`') {
                        p = parse_expansion(p, current_token, &pos, MAX_TOKEN_LEN, 1);
                    } else {
                        current_token[pos++] = *p++;
                    }
                }
                if (*p == '"') {
                    current_token[pos++] = *p++;
                } else {
                    fprintf(stderr, "Error: Unmatched double quote\n");
                    return;
                }
                continue;
            }
        }

        if ((*p == '$' || *p == '`') && !in_word) {
            if (!in_word) in_word = 1;
            p = parse_expansion(p, current_token, &pos, MAX_TOKEN_LEN, 0);
            continue;
        }

        if (isspace(*p) && *p != '\n') {
            if (in_word) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            p++;
            continue;
        }

        if (is_operator_start(*p)) {
            if (in_word) {
                current_token[pos] = '\0';
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
                in_word = 0;
            }
            current_token[pos++] = *p++;
            if (*p && is_operator_start(*p)) {
                current_token[pos++] = *p++;
            }
            current_token[pos] = '\0';
            if (is_operator(current_token)) {
                strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
                (*token_count)++;
                pos = 0;
            } else {
                pos = 0;
            }
            continue;
        }

        if (!in_word) in_word = 1;
        if (pos < MAX_TOKEN_LEN - 1) {
            current_token[pos++] = *p++;
        } else {
            current_token[pos] = '\0';
            strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
            (*token_count)++;
            pos = 0;
            in_word = 0;
        }
    }

    if (in_word) {
        current_token[pos] = '\0';
        strncpy(tokens[*token_count].text, current_token, MAX_TOKEN_LEN);
        (*token_count)++;
    }

    if (*token_count > 0) {
        char alias_value[MAX_TOKEN_LEN];
        if (substitute_alias(tokens[0].text, alias_value, MAX_TOKEN_LEN)) {
            strncpy(tokens[0].text, alias_value, MAX_TOKEN_LEN);
            if (strlen(alias_value) > 0 && isspace(alias_value[strlen(alias_value) - 1]) && *token_count > 1) {
                if (substitute_alias(tokens[1].text, alias_value, MAX_TOKEN_LEN)) {
                    strncpy(tokens[1].text, alias_value, MAX_TOKEN_LEN);
                }
            }
        }
    }

    for (int i = 0; i < *token_count; i++) {
        if (is_operator(tokens[i].text)) {
            tokens[i].type = get_operator_type(tokens[i].text);
        } else if (i + 1 < *token_count && is_all_digits(tokens[i].text) &&
                   (strcmp(tokens[i + 1].text, "<") == 0 || strcmp(tokens[i + 1].text, ">") == 0)) {
            tokens[i].type = IO_NUMBER;
        } else {
            tokens[i].type = TOKEN;
        }
    }

    for (int i = 0; i < *token_count; i++) {
        if (tokens[i].type == TOKEN) {
            if (i == 0 || (i > 0 && is_operator(tokens[i - 1].text))) {
                TokenType reserved = get_reserved_word_type(tokens[i].text);
                if (reserved != TOKEN) {
                    tokens[i].type = reserved;
                } else if (i + 1 < *token_count && (tokens[i + 1].type == DO || tokens[i + 1].type == LPAREN) && is_valid_name(tokens[i].text)) {
                    tokens[i].type = NAME;
                } else {
                    tokens[i].type = WORD;
                }
            } else if (i > 0 && tokens[i - 1].type != ASSIGNMENT_WORD && strchr(tokens[i].text, '=')) {
                char *eq = strchr(tokens[i].text, '=');
                if (eq != tokens[i].text) {
                    *eq = '\0';
                    if (is_valid_name(tokens[i].text)) {
                        tokens[i].type = ASSIGNMENT_WORD;
                    }
                    *eq = '=';
                }
                if (tokens[i].type == TOKEN) {
                    tokens[i].type = WORD;
                }
            } else {
                tokens[i].type = WORD;
            }
        }
    }
}

void init_parser_state(ParserState *state) {
    state->token_capacity = MAX_TOKENS;
    state->tokens = malloc(state->token_capacity * sizeof(Token));
    state->token_count = 0;
    state->pos = 0;
    state->brace_depth = 0;
    state->paren_depth = 0;
    state->expecting = 0;
}

void free_parser_state(ParserState *state) {
    free(state->tokens);
    state->tokens = NULL;
    state->token_count = 0;
    state->token_capacity = 0;
    state->pos = 0;
}

static Redirect *parse_redirect_list(ParserState *state) {
    Redirect *head = NULL, *tail = NULL;
    while (state->pos < state->token_count && (state->tokens[state->pos].type == IO_NUMBER || state->tokens[state->pos].type == LESS || 
           state->tokens[state->pos].type == GREAT || state->tokens[state->pos].type == DLESS || state->tokens[state->pos].type == DGREAT || 
           state->tokens[state->pos].type == LESSAND || state->tokens[state->pos].type == GREATAND || state->tokens[state->pos].type == LESSGREAT || 
           state->tokens[state->pos].type == CLOBBER)) {
        Redirect *redir = malloc(sizeof(Redirect));
        redir->io_number = NULL;
        redir->next = NULL;

        if (state->tokens[state->pos].type == IO_NUMBER) {
            redir->io_number = strdup(state->tokens[state->pos].text);
            (state->pos)++;
        }

        if (state->pos >= state->token_count || (state->tokens[state->pos].type != LESS && state->tokens[state->pos].type != GREAT && 
            state->tokens[state->pos].type != DLESS && state->tokens[state->pos].type != DGREAT && state->tokens[state->pos].type != LESSAND && 
            state->tokens[state->pos].type != GREATAND && state->tokens[state->pos].type != LESSGREAT && state->tokens[state->pos].type != CLOBBER)) {
            fprintf(stderr, "Error: Expected redirect operator after IO_NUMBER\n");
            free(redir->io_number);
            free(redir);
            while (head) {
                Redirect *next = head->next;
                free(head->io_number);
                free(head->filename);
                free(head);
                head = next;
            }
            return NULL;
        }

        redir->operator = state->tokens[state->pos].type;
        (state->pos)++;

        if (state->pos >= state->token_count || state->tokens[state->pos].type != WORD) {
            fprintf(stderr, "Error: Expected filename after redirect operator\n");
            free(redir->io_number);
            free(redir);
            while (head) {
                Redirect *next = head->next;
                free(head->io_number);
                free(head->filename);
                free(head);
                head = next;
            }
            return NULL;
        }
        redir->filename = strdup(state->tokens[state->pos].text);
        (state->pos)++;

        if (!head) {
            head = tail = redir;
        } else {
            tail->next = redir;
            tail = redir;
        }
    }
    return head;
}

static ASTNode *parse_simple_command(ParserState *state) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_SIMPLE_COMMAND;
    node->data.simple_command.prefix = NULL;
    node->data.simple_command.prefix_count = 0;
    node->data.simple_command.command = NULL;
    node->data.simple_command.suffix = NULL;
    node->data.simple_command.suffix_count = 0;
    node->data.simple_command.redirects = NULL;

    while (state->pos < state->token_count && (state->tokens[state->pos].type == ASSIGNMENT_WORD || state->tokens[state->pos].type == IO_NUMBER ||
           state->tokens[state->pos].type == LESS || state->tokens[state->pos].type == GREAT || state->tokens[state->pos].type == DLESS ||
           state->tokens[state->pos].type == DGREAT || state->tokens[state->pos].type == LESSAND || state->tokens[state->pos].type == GREATAND ||
           state->tokens[state->pos].type == LESSGREAT || state->tokens[state->pos].type == CLOBBER)) {
        if (state->tokens[state->pos].type == ASSIGNMENT_WORD) {
            node->data.simple_command.prefix = realloc(node->data.simple_command.prefix,
                (node->data.simple_command.prefix_count + 1) * sizeof(char *));
            node->data.simple_command.prefix[node->data.simple_command.prefix_count] = strdup(state->tokens[state->pos].text);
            node->data.simple_command.prefix_count++;
            (state->pos)++;
        } else {
            int prev_pos = state->pos;
            Redirect *redir = parse_redirect_list(state);
            if (redir) {
                if (!node->data.simple_command.redirects) {
                    node->data.simple_command.redirects = redir;
                } else {
                    Redirect *last = node->data.simple_command.redirects;
                    while (last->next) last = last->next;
                    last->next = redir;
                }
            } else {
                state->pos = prev_pos;
                break;
            }
        }
    }

    if (state->pos < state->token_count && state->tokens[state->pos].type == WORD) {
        node->data.simple_command.command = strdup(state->tokens[state->pos].text);
        (state->pos)++;
    }

    while (state->pos < state->token_count && (state->tokens[state->pos].type == WORD || state->tokens[state->pos].type == IO_NUMBER ||
           state->tokens[state->pos].type == LESS || state->tokens[state->pos].type == GREAT || state->tokens[state->pos].type == DLESS ||
           state->tokens[state->pos].type == DGREAT || state->tokens[state->pos].type == LESSAND || state->tokens[state->pos].type == GREATAND ||
           state->tokens[state->pos].type == LESSGREAT || state->tokens[state->pos].type == CLOBBER)) {
        if (state->tokens[state->pos].type == WORD) {
            node->data.simple_command.suffix = realloc(node->data.simple_command.suffix,
                (node->data.simple_command.suffix_count + 1) * sizeof(char *));
            node->data.simple_command.suffix[node->data.simple_command.suffix_count] = strdup(state->tokens[state->pos].text);
            node->data.simple_command.suffix_count++;
            (state->pos)++;
        } else {
            int prev_pos = state->pos;
            Redirect *redir = parse_redirect_list(state);
            if (redir) {
                if (!node->data.simple_command.redirects) {
                    node->data.simple_command.redirects = redir;
                } else {
                    Redirect *last = node->data.simple_command.redirects;
                    while (last->next) last = last->next;
                    last->next = redir;
                }
            } else {
                state->pos = prev_pos;
                break;
            }
        }
    }

    return node;
}

static ASTNode *parse_compound_list(ParserState *state);

static ASTNode *parse_if_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != IF) return NULL;
    (state->pos)++;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_IF_CLAUSE;
    node->data.if_clause.condition = parse_compound_list(state);

    if (state->pos >= state->token_count || state->tokens[state->pos].type != THEN) {
        fprintf(stderr, "Error: Expected 'then' in if clause\n");
        state->expecting |= EXPECTING_FI;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;

    node->data.if_clause.then_body = parse_compound_list(state);
    node->data.if_clause.else_part = NULL;

    if (state->pos < state->token_count && (state->tokens[state->pos].type == ELSE || state->tokens[state->pos].type == ELIF)) {
        if (state->tokens[state->pos].type == ELSE) {
            (state->pos)++;
            node->data.if_clause.else_part = parse_compound_list(state);
        } else {
            ASTNode *elif = malloc(sizeof(ASTNode));
            elif->type = AST_IF_CLAUSE;
            (state->pos)++;
            elif->data.if_clause.condition = parse_compound_list(state);
            if (state->pos >= state->token_count || state->tokens[state->pos].type != THEN) {
                fprintf(stderr, "Error: Expected 'then' after 'elif'\n");
                state->expecting |= EXPECTING_FI;
                free_ast(elif);
                free_ast(node);
                return NULL;
            }
            (state->pos)++;
            elif->data.if_clause.then_body = parse_compound_list(state);
            elif->data.if_clause.else_part = NULL;
            node->data.if_clause.else_part = elif;

            if (state->pos < state->token_count && (state->tokens[state->pos].type == ELSE || state->tokens[state->pos].type == ELIF)) {
                if (state->tokens[state->pos].type == ELSE) {
                    (state->pos)++;
                    elif->data.if_clause.else_part = parse_compound_list(state);
                } else {
                    ASTNode *nested_elif = parse_if_clause(state);
                    if (nested_elif) {
                        elif->data.if_clause.else_part = nested_elif;
                        while (nested_elif->data.if_clause.else_part && nested_elif->data.if_clause.else_part->type == AST_IF_CLAUSE) {
                            nested_elif = nested_elif->data.if_clause.else_part;
                        }
                        if (state->pos < state->token_count && state->tokens[state->pos].type == ELSE) {
                            (state->pos)++;
                            nested_elif->data.if_clause.else_part = parse_compound_list(state);
                        }
                    }
                }
            }
        }
    }

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_FI;
        return node;
    }
    if (state->tokens[state->pos].type != FI) {
        fprintf(stderr, "Error: Expected 'fi' in if clause\n");
        state->expecting |= EXPECTING_FI;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;
    state->expecting &= ~EXPECTING_FI;

    return node;
}

static ASTNode *parse_for_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != FOR) return NULL;
    (state->pos)++;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_FOR_CLAUSE;
    node->data.for_clause.variable = NULL;
    node->data.for_clause.wordlist = NULL;
    node->data.for_clause.wordlist_count = 0;
    node->data.for_clause.body = NULL;

    if (state->pos >= state->token_count || state->tokens[state->pos].type != NAME) {
        fprintf(stderr, "Error: Expected variable name in for clause\n");
        state->expecting |= EXPECTING_DONE;
        free(node);
        return NULL;
    }
    node->data.for_clause.variable = strdup(state->tokens[state->pos].text);
    (state->pos)++;

    if (state->pos < state->token_count && state->tokens[state->pos].type == IN) {
        (state->pos)++;
        while (state->pos < state->token_count && state->tokens[state->pos].type == WORD) {
            node->data.for_clause.wordlist = realloc(node->data.for_clause.wordlist,
                (node->data.for_clause.wordlist_count + 1) * sizeof(char *));
            node->data.for_clause.wordlist[node->data.for_clause.wordlist_count] = strdup(state->tokens[state->pos].text);
            node->data.for_clause.wordlist_count++;
            (state->pos)++;
        }
    }

    if (state->pos < state->token_count && state->tokens[state->pos].type == SEMI) (state->pos)++;

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_DONE;
        return node;
    }
    if (state->tokens[state->pos].type != DO) {
        fprintf(stderr, "Error: Expected 'do' in for clause\n");
        state->expecting |= EXPECTING_DONE;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;

    node->data.for_clause.body = parse_compound_list(state);

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_DONE;
        return node;
    }
    if (state->tokens[state->pos].type != DONE) {
        fprintf(stderr, "Error: Expected 'done' in for clause\n");
        state->expecting |= EXPECTING_DONE;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;
    state->expecting &= ~EXPECTING_DONE;

    return node;
}

static CaseItem *parse_case_item(ParserState *state) {
    CaseItem *item = malloc(sizeof(CaseItem));
    item->patterns = NULL;
    item->pattern_count = 0;
    item->action = NULL;
    item->has_dsemi = 0;
    item->next = NULL;

    if (state->pos < state->token_count && state->tokens[state->pos].type == LPAREN) (state->pos)++;

    while (state->pos < state->token_count && state->tokens[state->pos].type == WORD) {
        item->patterns = realloc(item->patterns, (item->pattern_count + 1) * sizeof(char *));
        item->patterns[item->pattern_count] = strdup(state->tokens[state->pos].text);
        item->pattern_count++;
        (state->pos)++;
        if (state->pos < state->token_count && state->tokens[state->pos].type == PIPE) (state->pos)++;
    }

    if (state->pos >= state->token_count || state->tokens[state->pos].type != RPAREN) {
        fprintf(stderr, "Error: Expected ')' in case item\n");
        state->expecting |= EXPECTING_ESAC;
        free(item->patterns);
        free(item);
        return NULL;
    }
    (state->pos)++;

    if (state->pos < state->token_count && state->tokens[state->pos].type != DSEMI) {
        item->action = parse_compound_list(state);
    }

    if (state->pos < state->token_count && state->tokens[state->pos].type == DSEMI) {
        item->has_dsemi = 1;
        (state->pos)++;
    }

    return item;
}

static ASTNode *parse_case_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != CASE) return NULL;
    (state->pos)++;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_CASE_CLAUSE;
    node->data.case_clause.word = NULL;
    node->data.case_clause.items = NULL;

    if (state->pos >= state->token_count || state->tokens[state->pos].type != WORD) {
        fprintf(stderr, "Error: Expected word in case clause\n");
        state->expecting |= EXPECTING_ESAC;
        free(node);
        return NULL;
    }
    node->data.case_clause.word = strdup(state->tokens[state->pos].text);
    (state->pos)++;

    if (state->pos >= state->token_count || state->tokens[state->pos].type != IN) {
        fprintf(stderr, "Error: Expected 'in' in case clause\n");
        state->expecting |= EXPECTING_ESAC;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;

    CaseItem *last_item = NULL;
    while (state->pos < state->token_count && state->tokens[state->pos].type != ESAC) {
        CaseItem *item = parse_case_item(state);
        if (!item) {
            fprintf(stderr, "Error: Failed to parse case item\n");
            state->expecting |= EXPECTING_ESAC;
            free_ast(node);
            return NULL;
        }
        if (!node->data.case_clause.items) {
            node->data.case_clause.items = item;
        } else {
            last_item->next = item;
        }
        last_item = item;
    }

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_ESAC;
        return node;
    }
    if (state->tokens[state->pos].type != ESAC) {
        fprintf(stderr, "Error: Expected 'esac' in case clause\n");
        state->expecting |= EXPECTING_ESAC;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;
    state->expecting &= ~EXPECTING_ESAC;

    return node;
}

static ASTNode *parse_while_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != WHILE) return NULL;
    (state->pos)++;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_WHILE_CLAUSE;
    node->data.while_clause.condition = parse_compound_list(state);

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_DONE;
        return node;
    }
    if (state->tokens[state->pos].type != DO) {
        fprintf(stderr, "Error: Expected 'do' in while clause\n");
        state->expecting |= EXPECTING_DONE;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;

    node->data.while_clause.body = parse_compound_list(state);

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_DONE;
        return node;
    }
    if (state->tokens[state->pos].type != DONE) {
        fprintf(stderr, "Error: Expected 'done' in while clause\n");
        state->expecting |= EXPECTING_DONE;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;
    state->expecting &= ~EXPECTING_DONE;

    return node;
}

static ASTNode *parse_until_clause(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != UNTIL) return NULL;
    (state->pos)++;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_UNTIL_CLAUSE;
    node->data.until_clause.condition = parse_compound_list(state);

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_DONE;
        return node;
    }
    if (state->tokens[state->pos].type != DO) {
        fprintf(stderr, "Error: Expected 'do' in until clause\n");
        state->expecting |= EXPECTING_DONE;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;

    node->data.until_clause.body = parse_compound_list(state);

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_DONE;
        return node;
    }
    if (state->tokens[state->pos].type != DONE) {
        fprintf(stderr, "Error: Expected 'done' in until clause\n");
        state->expecting |= EXPECTING_DONE;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;
    state->expecting &= ~EXPECTING_DONE;

    return node;
}

static ASTNode *parse_brace_group(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != LBRACE) return NULL;
    (state->pos)++;
    state->brace_depth++;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_BRACE_GROUP;
    node->data.brace_group.body = parse_compound_list(state);

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_RBRACE;
        return node;
    }
    if (state->tokens[state->pos].type != RBRACE) {
        fprintf(stderr, "Error: Expected '}' in brace group\n");
        state->expecting |= EXPECTING_RBRACE;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;
    state->brace_depth--;
    state->expecting &= ~EXPECTING_RBRACE;

    return node;
}

static ASTNode *parse_subshell(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != LPAREN) return NULL;
    (state->pos)++;
    state->paren_depth++;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_SUBSHELL;
    node->data.subshell.body = parse_compound_list(state);

    if (state->pos >= state->token_count) {
        state->expecting |= EXPECTING_RPAREN;
        return node;
    }
    if (state->tokens[state->pos].type != RPAREN) {
        fprintf(stderr, "Error: Expected ')' in subshell\n");
        state->expecting |= EXPECTING_RPAREN;
        free_ast(node);
        return NULL;
    }
    (state->pos)++;
    state->paren_depth--;
    state->expecting &= ~EXPECTING_RPAREN;

    return node;
}

static ASTNode *parse_function_definition(ParserState *state) {
    if (state->pos >= state->token_count || state->tokens[state->pos].type != NAME || 
        state->pos + 1 >= state->token_count || state->tokens[state->pos + 1].type != LPAREN) return NULL;
    
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_FUNCTION_DEFINITION;
    node->data.function_definition.name = strdup(state->tokens[state->pos].text);
    node->data.function_definition.redirects = NULL;
    (state->pos)++; // Skip NAME
    (state->pos)++; // Skip (

    if (state->pos >= state->token_count || state->tokens[state->pos].type != RPAREN) {
        fprintf(stderr, "Error: Expected ')' in function definition\n");
        state->expecting |= EXPECTING_RBRACE; // Assuming brace group follows
        free_ast(node);
        return NULL;
    }
    (state->pos)++;

    node->data.function_definition.body = parse_compound_list(state);

    if (state->pos >= state->token_count && (state->brace_depth > 0 || state->paren_depth > 0)) {
        state->expecting |= EXPECTING_RBRACE;
        return node;
    }

    if (state->pos < state->token_count && (state->tokens[state->pos].type == IO_NUMBER || state->tokens[state->pos].type == LESS || 
        state->tokens[state->pos].type == GREAT || state->tokens[state->pos].type == DLESS || state->tokens[state->pos].type == DGREAT || 
        state->tokens[state->pos].type == LESSAND || state->tokens[state->pos].type == GREATAND || state->tokens[state->pos].type == LESSGREAT || 
        state->tokens[state->pos].type == CLOBBER)) {
        node->data.function_definition.redirects = parse_redirect_list(state);
    }

    return node;
}

static ASTNode *parse_compound_command(ParserState *state) {
    if (state->pos >= state->token_count) return NULL;
    if (state->tokens[state->pos].type == LBRACE) return parse_brace_group(state);
    if (state->tokens[state->pos].type == LPAREN) return parse_subshell(state);
    if (state->tokens[state->pos].type == FOR) return parse_for_clause(state);
    if (state->tokens[state->pos].type == CASE) return parse_case_clause(state);
    if (state->tokens[state->pos].type == IF) return parse_if_clause(state);
    if (state->tokens[state->pos].type == WHILE) return parse_while_clause(state);
    if (state->tokens[state->pos].type == UNTIL) return parse_until_clause(state);
    return NULL;
}

static ASTNode *parse_command(ParserState *state) {
    ASTNode *cmd = NULL;
    if (state->pos < state->token_count && state->tokens[state->pos].type == NAME && 
        state->pos + 1 < state->token_count && state->tokens[state->pos + 1].type == LPAREN) {
        cmd = parse_function_definition(state);
    } else {
        cmd = parse_compound_command(state);
        if (!cmd) {
            cmd = parse_simple_command(state);
        }
    }

    if (cmd && state->pos < state->token_count && (state->tokens[state->pos].type == IO_NUMBER || state->tokens[state->pos].type == LESS || 
        state->tokens[state->pos].type == GREAT || state->tokens[state->pos].type == DLESS || state->tokens[state->pos].type == DGREAT || 
        state->tokens[state->pos].type == LESSAND || state->tokens[state->pos].type == GREATAND || state->tokens[state->pos].type == LESSGREAT || 
        state->tokens[state->pos].type == CLOBBER)) {
        Redirect *redir = parse_redirect_list(state);
        if (cmd->type == AST_SIMPLE_COMMAND) {
            cmd->data.simple_command.redirects = redir;
        } else if (cmd->type == AST_FUNCTION_DEFINITION) {
            cmd->data.function_definition.redirects = redir;
        }
    }

    return cmd;
}

static ASTNode *parse_pipeline(ParserState *state) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_PIPELINE;
    node->data.pipeline.commands = NULL;
    node->data.pipeline.command_count = 0;
    node->data.pipeline.bang = 0;

    if (state->pos < state->token_count && state->tokens[state->pos].type == BANG) {
        node->data.pipeline.bang = 1;
        (state->pos)++;
    }

    while (state->pos < state->token_count) {
        ASTNode *cmd = parse_command(state);
        if (!cmd) break;
        node->data.pipeline.commands = realloc(node->data.pipeline.commands,
            (node->data.pipeline.command_count + 1) * sizeof(ASTNode *));
        node->data.pipeline.commands[node->data.pipeline.command_count] = cmd;
        node->data.pipeline.command_count++;

        if (state->pos >= state->token_count || state->tokens[state->pos].type != PIPE) break;
        (state->pos)++;
    }

    return node;
}

static ASTNode *parse_and_or(ParserState *state) {
    ASTNode *left = parse_pipeline(state);
    if (!left || state->pos >= state->token_count || (state->tokens[state->pos].type != AND_IF && state->tokens[state->pos].type != OR_IF)) {
        return left;
    }

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_AND_OR;
    node->data.and_or.left = left;
    node->data.and_or.operator = state->tokens[state->pos].type;
    (state->pos)++;
    node->data.and_or.right = parse_pipeline(state);

    return node;
}

static ASTNode *parse_term(ParserState *state) {
    ASTNode *and_or = parse_and_or(state);
    if (!and_or || state->pos >= state->token_count || (state->tokens[state->pos].type != SEMI && state->tokens[state->pos].type != AMP)) {
        return and_or;
    }

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_LIST;
    node->data.list.and_or = and_or;
    node->data.list.separator = state->tokens[state->pos].type;
    (state->pos)++;
    node->data.list.next = parse_term(state);

    return node;
}

static ASTNode *parse_compound_list(ParserState *state) {
    while (state->pos < state->token_count && state->tokens[state->pos].type == NEWLINE) (state->pos)++;
    ASTNode *term = parse_term(state);
    if (!term) return NULL;
    if (state->pos < state->token_count && (state->tokens[state->pos].type == SEMI || state->tokens[state->pos].type == AMP)) {
        ASTNode *node = malloc(sizeof(ASTNode));
        node->type = AST_LIST;
        node->data.list.and_or = term;
        node->data.list.separator = state->tokens[state->pos].type;
        (state->pos)++;
        node->data.list.next = NULL;
        return node;
    }
    return term;
}

static ASTNode *parse_list(ParserState *state) {
    ASTNode *and_or = parse_and_or(state);
    if (!and_or) return NULL;
    if (state->pos >= state->token_count || (state->tokens[state->pos].type != SEMI && state->tokens[state->pos].type != AMP)) {
        ASTNode *node = malloc(sizeof(ASTNode));
        node->type = AST_LIST;
        node->data.list.and_or = and_or;
        node->data.list.separator = TOKEN;
        node->data.list.next = NULL;
        return node;
    }

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_LIST;
    node->data.list.and_or = and_or;
    node->data.list.separator = state->tokens[state->pos].type;
    (state->pos)++;
    node->data.list.next = parse_list(state);

    return node;
}

static ASTNode *parse_complete_command(ParserState *state) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_COMPLETE_COMMAND;
    node->data.complete_command.list = parse_list(state);
    if (!node->data.complete_command.list) {
        free(node);
        return NULL;
    }
    node->data.complete_command.separator = (state->pos < state->token_count && 
        (state->tokens[state->pos].type == SEMI || state->tokens[state->pos].type == AMP)) ? state->tokens[state->pos].type : TOKEN;
    if (node->data.complete_command.separator != TOKEN) (state->pos)++;
    return node;
}

static ASTNode *parse_complete_commands(ParserState *state) {
    ASTNode *first = parse_complete_command(state);
    if (!first || state->pos >= state->token_count || state->tokens[state->pos].type != NEWLINE) return first;

    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_LIST;
    node->data.list.and_or = first;
    node->data.list.separator = NEWLINE;
    while (state->pos < state->token_count && state->tokens[state->pos].type == NEWLINE) (state->pos)++;
    node->data.list.next = parse_complete_commands(state);
    return node;
}

ParseStatus parse_line(const char *line, ParserState *state, ASTNode **ast) {
    Token new_tokens[MAX_TOKENS];
    int new_token_count = 0;
    tokenize(line, new_tokens, &new_token_count);

    if (state->token_count + new_token_count > state->token_capacity) {
        state->token_capacity = (state->token_count + new_token_count) * 2;
        state->tokens = realloc(state->tokens, state->token_capacity * sizeof(Token));
    }
    memcpy(state->tokens + state->token_count, new_tokens, new_token_count * sizeof(Token));
    state->token_count += new_token_count;

    state->pos = 0;

    while (state->pos < state->token_count && state->tokens[state->pos].type == NEWLINE) state->pos++;

    if (state->pos >= state->token_count && state->expecting == 0) {
        *ast = NULL;
        return PARSE_COMPLETE;
    }

    *ast = malloc(sizeof(ASTNode));
    (*ast)->type = AST_PROGRAM;
    (*ast)->data.program.commands = parse_complete_commands(state);

    if (state->pos < state->token_count) {
        fprintf(stderr, "Error: Incomplete parsing, extra tokens remain\n");
        free_ast(*ast);
        *ast = NULL;
        return PARSE_ERROR;
    }

    if (state->brace_depth > 0 || state->paren_depth > 0 || state->expecting != 0) {
        return PARSE_INCOMPLETE;
    }

    return PARSE_COMPLETE;
}

void init_environment(Environment *env) {
    env->var_capacity = 16;
    env->variables = malloc(env->var_capacity * sizeof(char *));
    env->var_count = 0;
}

void free_environment(Environment *env) {
    for (int i = 0; i < env->var_count; i++) free(env->variables[i]);
    free(env->variables);
}

char *expand_tilde(const char *value) {
    if (value && value[0] == '~') {
        return strdup(value); // Placeholder: no HOME in C23
    }
    return strdup(value ? value : "");
}

char *expand_parameter(const char *value, Environment *env) {
    if (!value || value[0] != '$') return strdup(value ? value : "");
    const char *var_name = value + 1;
    if (var_name[0] == '{') {
        var_name++;
        const char *end = strchr(var_name, '}');
        if (!end) return strdup("");
        size_t len = end - var_name;
        char name[len + 1];
        strncpy(name, var_name, len);
        name[len] = '\0';
        const char *val = get_variable(env, name);
        return strdup(val ? val : "");
    }
    const char *val = get_variable(env, var_name);
    return strdup(val ? val : "");
}

char *expand_command_substitution(const char *value, Environment *env, FunctionTable *ft, int *last_exit_status) {
    if (!value || (strncmp(value, "$(", 2) != 0 && value[0] != '`')) return strdup(value ? value : "");

    char command[MAX_COMMAND_LEN];
    const char *start = value;
    const char *end;
    if (value[0] == '$') {
        start += 2;
        end = strchr(start, ')');
    } else {
        start++;
        end = strchr(start, '`');
    }
    if (!end) return strdup("");

    size_t len = end - start;
    if (len >= MAX_COMMAND_LEN) len = MAX_COMMAND_LEN - 1;
    strncpy(command, start, len);
    command[len] = '\0';

    char temp_file[] = "sh23_temp_XXXXXX";
    int fd = mkstemp(temp_file);
    if (fd == -1) {
        fprintf(stderr, "Error: Could not create temp file for command substitution\n");
        return strdup("");
    }
    close(fd);

    char redirected_command[MAX_COMMAND_LEN];
    snprintf(redirected_command, MAX_COMMAND_LEN, "%s > %s", command, temp_file);
    int status = system(redirected_command);
    if (status != 0) {
        remove(temp_file);
        return strdup("");
    }

    FILE *file = fopen(temp_file, "r");
    if (!file) {
        remove(temp_file);
        return strdup("");
    }

    char result[MAX_COMMAND_LEN];
    size_t bytes_read = fread(result, 1, MAX_COMMAND_LEN - 1, file);
    result[bytes_read] = '\0';
    fclose(file);
    remove(temp_file);

    char *nl = strchr(result, '\n');
    if (nl) *nl = '\0';

    return strdup(result);
}

char *expand_arithmetic(const char *value, Environment *env) {
    if (!value || strncmp(value, "$((", 3) != 0) return strdup(value ? value : "");

    const char *start = value + 3;
    const char *end = strstr(start, "))");
    if (!end) return strdup("");

    size_t len = end - start;
    char expr[len + 1];
    strncpy(expr, start, len);
    expr[len] = '\0';

    int result = 0;
    char op = '+';
    char *p = expr;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!isdigit((unsigned char)*p)) {
            if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
                op = *p++;
                continue;
            }
            break;
        }
        int num = 0;
        while (isdigit((unsigned char)*p)) {
            num = num * 10 + (*p - '0');
            p++;
        }
        switch (op) {
            case '+': result += num; break;
            case '-': result -= num; break;
            case '*': result *= num; break;
            case '/': result = num ? result / num : 0; break;
        }
    }

    char result_str[32];
    snprintf(result_str, sizeof(result_str), "%d", result);
    return strdup(result_str);
}

char *remove_quotes(const char *value) {
    if (!value) return strdup("");
    size_t len = strlen(value);
    char *result = malloc(len + 1);
    size_t j = 0;
    int in_single = 0, in_double = 0;
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '\\' && i + 1 < len) {
            result[j++] = value[++i];
            continue;
        }
        if (value[i] == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (value[i] == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        result[j++] = value[i];
    }
    result[j] = '\0';
    return result;
}

char *expand_assignment(const char *assignment, Environment *env, FunctionTable *ft, int *last_exit_status) {
    char *equals = strchr(assignment, '=');
    if (!equals) return strdup(assignment);
    size_t name_len = equals - assignment;
    char name[name_len + 1];
    strncpy(name, assignment, name_len);
    name[name_len] = '\0';
    const char *value = equals + 1;

    char *expanded = expand_tilde(value);
    char *temp = expanded;

    if (strchr(expanded, '$') && strncmp(expanded, "$((", 3) != 0) {
        free(expanded);
        expanded = expand_parameter(temp, env);
        free(temp);
        temp = expanded;
    }

    if (strncmp(expanded, "$(", 2) == 0 || strchr(expanded, '`')) {
        free(expanded);
        expanded = expand_command_substitution(temp, env, ft, last_exit_status);
        free(temp);
        temp = expanded;
    }

    if (strncmp(expanded, "$((", 3) == 0) {
        free(expanded);
        expanded = expand_arithmetic(temp, env);
        free(temp);
        temp = expanded;
    }

    expanded = remove_quotes(temp);
    free(temp);

    char *result = malloc(strlen(name) + strlen(expanded) + 2);
    sprintf(result, "%s=%s", name, expanded);
    free(expanded);
    return result;
}

void set_variable(Environment *env, const char *assignment) {
    char *expanded = expand_assignment(assignment, env, NULL, NULL);
    char *name = strdup(expanded);
    char *value = strchr(name, '=');
    if (value) {
        *value = '\0';
        value++;
    } else {
        value = "";
    }
    for (int i = 0; i < env->var_count; i++) {
        if (strncmp(env->variables[i], name, strlen(name)) == 0 && env->variables[i][strlen(name)] == '=') {
            free(env->variables[i]);
            env->variables[i] = strdup(expanded);
            free(name);
            free(expanded);
            return;
        }
    }
    if (env->var_count >= env->var_capacity) {
        env->var_capacity *= 2;
        env->variables = realloc(env->variables, env->var_capacity * sizeof(char *));
    }
    env->variables[env->var_count] = strdup(expanded);
    env->var_count++;
    free(name);
    free(expanded);
}

const char *get_variable(Environment *env, const char *name) {
    for (int i = 0; i < env->var_count; i++) {
        if (strncmp(env->variables[i], name, strlen(name)) == 0 && env->variables[i][strlen(name)] == '=') {
            return env->variables[i] + strlen(name) + 1;
        }
    }
    return NULL;
}

void init_function_table(FunctionTable *ft) {
    ft->func_capacity = 16;
    ft->functions = malloc(ft->func_capacity * sizeof(Function));
    ft->func_count = 0;
}

void free_function_table(FunctionTable *ft) {
    for (int i = 0; i < ft->func_count; i++) {
        free(ft->functions[i].name);
        free_ast(ft->functions[i].body);
        Redirect *redir = ft->functions[i].redirects;
        while (redir) {
            free(redir->io_number);
            free(redir->filename);
            Redirect *next = redir->next;
            free(redir);
            redir = next;
        }
    }
    free(ft->functions);
}

void add_function(FunctionTable *ft, const char *name, ASTNode *body, Redirect *redirects) {
    for (int i = 0; i < ft->func_count; i++) {
        if (strcmp(ft->functions[i].name, name) == 0) {
            free(ft->functions[i].name);
            free_ast(ft->functions[i].body);
            Redirect *redir = ft->functions[i].redirects;
            while (redir) {
                free(redir->io_number);
                free(redir->filename);
                Redirect *next = redir->next;
                free(redir);
                redir = next;
            }
            ft->functions[i].name = strdup(name);
            ft->functions[i].body = body;
            ft->functions[i].redirects = redirects;
            return;
        }
    }
    if (ft->func_count >= ft->func_capacity) {
        ft->func_capacity *= 2;
        ft->functions = realloc(ft->functions, ft->func_capacity * sizeof(Function));
    }
    ft->functions[ft->func_count].name = strdup(name);
    ft->functions[ft->func_count].body = body;
    ft->functions[ft->func_count].redirects = redirects;
    ft->func_count++;
}

ASTNode *get_function_body(FunctionTable *ft, const char *name) {
    for (int i = 0; i < ft->func_count; i++) {
        if (strcmp(ft->functions[i].name, name) == 0) return ft->functions[i].body;
    }
    return NULL;
}

Redirect *get_function_redirects(FunctionTable *ft, const char *name) {
    for (int i = 0; i < ft->func_count; i++) {
        if (strcmp(ft->functions[i].name, name) == 0) return ft->functions[i].redirects;
    }
    return NULL;
}

void show_variables(Environment *env) {
    if (env->var_count == 0) {
        printf("No variables set.\n");
        return;
    }
    for (int i = 0; i < env->var_count; i++) {
        printf("%s\n", env->variables[i]);
    }
}

char *get_shebang_interpreter(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return NULL;

    char buffer[MAX_SHEBANG_LEN];
    size_t bytes_read = fread(buffer, 1, MAX_SHEBANG_LEN - 1, file);
    buffer[bytes_read] = '\0';
    fclose(file);

    char *shebang = NULL;
    if (bytes_read >= 2 && strncmp(buffer, "#!", 2) == 0) {
        shebang = buffer + 2;
    } else if (bytes_read >= 5 && strncmp(buffer, "\xEF\xBB\xBF#!", 5) == 0) {
        shebang = buffer + 5;
    } else {
        return NULL;
    }

    char *end = strchr(shebang, '\n');
    if (end) *end = '\0';
    while (*shebang && isspace((unsigned char)*shebang)) shebang++;
    char *interp_end = shebang;
    while (*interp_end && !isspace((unsigned char)*interp_end)) interp_end++;
    if (*interp_end) *interp_end = '\0';

    return strdup(shebang);
}

int has_redirects(Redirect *redirects) {
    return redirects != NULL;
}

int execute_simple_command(ASTNode *node, Environment *env, FunctionTable *ft, int *last_exit_status) {
    for (int i = 0; i < node->data.simple_command.prefix_count; i++) {
        set_variable(env, node->data.simple_command.prefix[i]);
    }
    if (!node->data.simple_command.command) return 0;

    if (strcmp(node->data.simple_command.command, "%showvars") == 0) {
        show_variables(env);
        return 0;
    }

    if (has_redirects(node->data.simple_command.redirects)) {
        fprintf(stderr, "Error: I/O redirection not supported in C23\n");
        return 1;
    }

    ASTNode *func_body = get_function_body(ft, node->data.simple_command.command);
    if (func_body) {
        if (has_redirects(get_function_redirects(ft, node->data.simple_command.command))) {
            fprintf(stderr, "Error: Function %s uses unsupported I/O redirection\n", node->data.simple_command.command);
            return 1;
        }
        execute_ast(func_body, env, ft, last_exit_status);
        return *last_exit_status;
    }

    char command[MAX_COMMAND_LEN] = {0};
    char *interpreter = get_shebang_interpreter(node->data.simple_command.command);
    if (interpreter) {
        strncat(command, interpreter, MAX_COMMAND_LEN - strlen(command) - 1);
        strncat(command, " ", MAX_COMMAND_LEN - strlen(command) - 1);
        free(interpreter);
    }
    strncat(command, node->data.simple_command.command, MAX_COMMAND_LEN - strlen(command) - 1);
    for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
        char *expanded = expand_assignment(node->data.simple_command.suffix[i], env, ft, last_exit_status);
        char *equals = strchr(expanded, '=');
        if (equals) *equals = '\0';
        strncat(command, " ", MAX_COMMAND_LEN - strlen(command) - 1);
        strncat(command, equals ? equals + 1 : expanded, MAX_COMMAND_LEN - strlen(command) - 1);
        free(expanded);
    }

    *last_exit_status = system(command);
    return *last_exit_status;
}

void execute_ast(ASTNode *ast, Environment *env, FunctionTable *ft, int *last_exit_status) {
    if (!ast) return;

    switch (ast->type) {
        case AST_PROGRAM:
            execute_ast(ast->data.program.commands, env, ft, last_exit_status);
            break;
        case AST_COMPLETE_COMMAND:
            execute_ast(ast->data.complete_command.list, env, ft, last_exit_status);
            break;
        case AST_LIST:
            execute_ast(ast->data.list.and_or, env, ft, last_exit_status);
            if (ast->data.list.next) {
                if (ast->data.list.separator == AMP) {
                    fprintf(stderr, "Error: Background execution (&) not supported in C23\n");
                    *last_exit_status = 1;
                } else {
                    execute_ast(ast->data.list.next, env, ft, last_exit_status);
                }
            }
            break;
        case AST_AND_OR:
            execute_ast(ast->data.and_or.left, env, ft, last_exit_status);
            if (ast->data.and_or.operator == AND_IF && *last_exit_status == 0) {
                execute_ast(ast->data.and_or.right, env, ft, last_exit_status);
            } else if (ast->data.and_or.operator == OR_IF && *last_exit_status != 0) {
                execute_ast(ast->data.and_or.right, env, ft, last_exit_status);
            }
            break;
        case AST_PIPELINE:
            if (ast->data.pipeline.command_count > 1) {
                fprintf(stderr, "Error: Pipelines (|) not supported in C23\n");
                *last_exit_status = 1;
                break;
            }
            execute_ast(ast->data.pipeline.commands[0], env, ft, last_exit_status);
            if (ast->data.pipeline.bang) *last_exit_status = !*last_exit_status;
            break;
        case AST_SIMPLE_COMMAND:
            execute_simple_command(ast, env, ft, last_exit_status);
            break;
        case AST_FUNCTION_DEFINITION:
            if (has_redirects(ast->data.function_definition.redirects)) {
                fprintf(stderr, "Error: Function %s definition uses unsupported I/O redirection\n", ast->data.function_definition.name);
                *last_exit_status = 1;
            } else {
                add_function(ft, ast->data.function_definition.name, ast->data.function_definition.body, ast->data.function_definition.redirects);
                ast->data.function_definition.body = NULL;
                ast->data.function_definition.redirects = NULL;
            }
            break;
        case AST_IF_CLAUSE:
            execute_ast(ast->data.if_clause.condition, env, ft, last_exit_status);
            if (*last_exit_status == 0) {
                execute_ast(ast->data.if_clause.then_body, env, ft, last_exit_status);
            } else if (ast->data.if_clause.else_part) {
                execute_ast(ast->data.if_clause.else_part, env, ft, last_exit_status);
            }
            break;
        case AST_FOR_CLAUSE:
            for (int i = 0; i < ast->data.for_clause.wordlist_count; i++) {
                char *assignment = malloc(strlen(ast->data.for_clause.variable) + strlen(ast->data.for_clause.wordlist[i]) + 2);
                sprintf(assignment, "%s=%s", ast->data.for_clause.variable, ast->data.for_clause.wordlist[i]);
                set_variable(env, assignment);
                free(assignment);
                execute_ast(ast->data.for_clause.body, env, ft, last_exit_status);
            }
            break;
        case AST_CASE_CLAUSE:
            {
                char *word = expand_assignment(ast->data.case_clause.word, env, ft, last_exit_status);
                char *equals = strchr(word, '=');
                if (equals) *equals = '\0';
                CaseItem *item = ast->data.case_clause.items;
                while (item) {
                    for (int i = 0; i < item->pattern_count; i++) {
                        if (strcmp(equals ? equals + 1 : word, item->patterns[i]) == 0) {
                            if (item->action) execute_ast(item->action, env, ft, last_exit_status);
                            free(word);
                            return;
                        }
                    }
                    item = item->next;
                }
                free(word);
            }
            break;
        case AST_WHILE_CLAUSE:
            while (1) {
                execute_ast(ast->data.while_clause.condition, env, ft, last_exit_status);
                if (*last_exit_status != 0) break;
                execute_ast(ast->data.while_clause.body, env, ft, last_exit_status);
            }
            break;
        case AST_UNTIL_CLAUSE:
            while (1) {
                execute_ast(ast->data.until_clause.condition, env, ft, last_exit_status);
                if (*last_exit_status == 0) break;
                execute_ast(ast->data.until_clause.body, env, ft, last_exit_status);
            }
            break;
        case AST_BRACE_GROUP:
            execute_ast(ast->data.brace_group.body, env, ft, last_exit_status);
            break;
        case AST_SUBSHELL:
            execute_ast(ast->data.subshell.body, env, ft, last_exit_status);
            break;
        case AST_IO_REDIRECT:
            fprintf(stderr, "Error: Standalone I/O redirection not supported in C23\n");
            *last_exit_status = 1;
            break;
    }
}

void free_ast(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_SIMPLE_COMMAND:
            for (int i = 0; i < node->data.simple_command.prefix_count; i++) free(node->data.simple_command.prefix[i]);
            free(node->data.simple_command.prefix);
            if (node->data.simple_command.command) free(node->data.simple_command.command);
            for (int i = 0; i < node->data.simple_command.suffix_count; i++) free(node->data.simple_command.suffix[i]);
            free(node->data.simple_command.suffix);
            Redirect *redir = node->data.simple_command.redirects;
            while (redir) {
                free(redir->io_number);
                free(redir->filename);
                Redirect *next = redir->next;
                free(redir);
                redir = next;
            }
            break;
        case AST_PIPELINE:
            for (int i = 0; i < node->data.pipeline.command_count; i++) free_ast(node->data.pipeline.commands[i]);
            free(node->data.pipeline.commands);
            break;
        case AST_AND_OR:
            free_ast(node->data.and_or.left);
            free_ast(node->data.and_or.right);
            break;
        case AST_LIST:
            free_ast(node->data.list.and_or);
            free_ast(node->data.list.next);
            break;
        case AST_COMPLETE_COMMAND:
            free_ast(node->data.complete_command.list);
            break;
        case AST_PROGRAM:
            free_ast(node->data.program.commands);
            break;
        case AST_IF_CLAUSE:
            free_ast(node->data.if_clause.condition);
            free_ast(node->data.if_clause.then_body);
            free_ast(node->data.if_clause.else_part);
            break;
        case AST_FOR_CLAUSE:
            if (node->data.for_clause.variable) free(node->data.for_clause.variable);
            for (int i = 0; i < node->data.for_clause.wordlist_count; i++) free(node->data.for_clause.wordlist[i]);
            free(node->data.for_clause.wordlist);
            free_ast(node->data.for_clause.body);
            break;
        case AST_CASE_CLAUSE:
            if (node->data.case_clause.word) free(node->data.case_clause.word);
            CaseItem *item = node->data.case_clause.items;
            while (item) {
                for (int i = 0; i < item->pattern_count; i++) free(item->patterns[i]);
                free(item->patterns);
                free_ast(item->action);
                CaseItem *next = item->next;
                free(item);
                item = next;
            }
            break;
        case AST_WHILE_CLAUSE:
            free_ast(node->data.while_clause.condition);
            free_ast(node->data.while_clause.body);
            break;
        case AST_UNTIL_CLAUSE:
            free_ast(node->data.until_clause.condition);
            free_ast(node->data.until_clause.body);
            break;
        case AST_BRACE_GROUP:
            free_ast(node->data.brace_group.body);
            break;
        case AST_SUBSHELL:
            free_ast(node->data.subshell.body);
            break;
        case AST_FUNCTION_DEFINITION:
            free(node->data.function_definition.name);
            free_ast(node->data.function_definition.body);
            redir = node->data.function_definition.redirects;
            while (redir) {
                free(redir->io_number);
                free(redir->filename);
                Redirect *next = redir->next;
                free(redir);
                redir = next;
            }
            break;
        case AST_IO_REDIRECT:
            free(node->data.io_redirect.io_number);
            free(node->data.io_redirect.filename);
            break;
    }
    free(node);
}


void print_ast(ASTNode *node, int depth) {
    if (!node) return;
    for (int i = 0; i < depth; i++) printf("  ");
    switch (node->type) {
        case AST_SIMPLE_COMMAND:
            printf("SIMPLE_COMMAND\n");
            for (int i = 0; i < node->data.simple_command.prefix_count; i++) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("PREFIX: %s\n", node->data.simple_command.prefix[i]);
            }
            if (node->data.simple_command.command) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("COMMAND: %s\n", node->data.simple_command.command);
            }
            for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("SUFFIX: %s\n", node->data.simple_command.suffix[i]);
            }
            Redirect *redir = node->data.simple_command.redirects;
            while (redir) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("REDIRECT: %s%s %s\n", redir->io_number ? redir->io_number : "",
                       redir->operator == LESS ? "<" : redir->operator == GREAT ? ">" : "??", redir->filename);
                redir = redir->next;
            }
            break;
        case AST_PIPELINE:
            printf("PIPELINE (bang: %d)\n", node->data.pipeline.bang);
            for (int i = 0; i < node->data.pipeline.command_count; i++) {
                print_ast(node->data.pipeline.commands[i], depth + 1);
            }
            break;
        case AST_AND_OR:
            printf("AND_OR (%s)\n", node->data.and_or.operator == AND_IF ? "&&" : "||");
            print_ast(node->data.and_or.left, depth + 1);
            print_ast(node->data.and_or.right, depth + 1);
            break;
        case AST_LIST:
            printf("LIST (sep: %s)\n", node->data.list.separator == SEMI ? ";" : node->data.list.separator == AMP ? "&" : node->data.list.separator == NEWLINE ? "\\n" : "none");
            print_ast(node->data.list.and_or, depth + 1);
            print_ast(node->data.list.next, depth + 1);
            break;
        case AST_COMPLETE_COMMAND:
            printf("COMPLETE_COMMAND (sep: %s)\n", node->data.complete_command.separator == SEMI ? ";" : node->data.complete_command.separator == AMP ? "&" : "none");
            print_ast(node->data.complete_command.list, depth + 1);
            break;
        case AST_PROGRAM:
            printf("PROGRAM\n");
            print_ast(node->data.program.commands, depth + 1);
            break;
        case AST_IF_CLAUSE:
            printf("IF_CLAUSE\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("CONDITION:\n");
            print_ast(node->data.if_clause.condition, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("THEN:\n");
            print_ast(node->data.if_clause.then_body, depth + 2);
            if (node->data.if_clause.else_part) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("ELSE:\n");
                print_ast(node->data.if_clause.else_part, depth + 2);
            }
            break;
        case AST_FOR_CLAUSE:
            printf("FOR_CLAUSE (var: %s)\n", node->data.for_clause.variable);
            if (node->data.for_clause.wordlist_count > 0) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("WORDLIST:");
                for (int i = 0; i < node->data.for_clause.wordlist_count; i++) {
                    printf(" %s", node->data.for_clause.wordlist[i]);
                }
                printf("\n");
            }
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("BODY:\n");
            print_ast(node->data.for_clause.body, depth + 2);
            break;
        case AST_CASE_CLAUSE:
            printf("CASE_CLAUSE (word: %s)\n", node->data.case_clause.word);
            CaseItem *item = node->data.case_clause.items;
            while (item) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("ITEM (patterns:");
                for (int j = 0; j < item->pattern_count; j++) {
                    printf(" %s", item->patterns[j]);
                }
                printf(") (dsemi: %d)\n", item->has_dsemi);
                if (item->action) print_ast(item->action, depth + 2);
                item = item->next;
            }
            break;
        case AST_WHILE_CLAUSE:
            printf("WHILE_CLAUSE\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("CONDITION:\n");
            print_ast(node->data.while_clause.condition, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("BODY:\n");
            print_ast(node->data.while_clause.body, depth + 2);
            break;
        case AST_UNTIL_CLAUSE:
            printf("UNTIL_CLAUSE\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("CONDITION:\n");
            print_ast(node->data.until_clause.condition, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("BODY:\n");
            print_ast(node->data.until_clause.body, depth + 2);
            break;
        case AST_BRACE_GROUP:
            printf("BRACE_GROUP\n");
            print_ast(node->data.brace_group.body, depth + 1);
            break;
        case AST_SUBSHELL:
            printf("SUBSHELL\n");
            print_ast(node->data.subshell.body, depth + 1);
            break;
        case AST_FUNCTION_DEFINITION:
            printf("FUNCTION_DEFINITION (name: %s)\n", node->data.function_definition.name);
            print_ast(node->data.function_definition.body, depth + 1);
            redir = node->data.function_definition.redirects;
            while (redir) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("REDIRECT: %s%s %s\n", redir->io_number ? redir->io_number : "",
                       redir->operator == LESS ? "<" : redir->operator == GREAT ? ">" : "??", redir->filename);
                redir = redir->next;
            }
            break;
        case AST_IO_REDIRECT:
            printf("IO_REDIRECT: %s%s %s\n", node->data.io_redirect.io_number ? node->data.io_redirect.io_number : "",
                   node->data.io_redirect.operator == LESS ? "<" : node->data.io_redirect.operator == GREAT ? ">" : "??",
                   node->data.io_redirect.filename);
            break;
    }
}
