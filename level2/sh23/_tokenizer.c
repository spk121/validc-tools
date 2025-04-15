#include "_tokenizer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "string.h"

#define INITIAL_TOKEN_CAPACITY 16
#define MAX_RECURSION_DEPTH 100
#define MAX_LINE_LENGTH 1024

// Create tokenizer
Tokenizer *tokenizer_create(void)
{
    Tokenizer *tokenizer = malloc(sizeof(Tokenizer));
    if (!tokenizer)
        return NULL;

    tokenizer->current_token = string_create_empty(INITIAL_TOKEN_CAPACITY);
    tokenizer->tokens = ptr_array_create_with_free(token_free);
    tokenizer->heredoc_delim = NULL;
    tokenizer->heredoc_content = NULL;
    tokenizer->in_quotes = 0;
    tokenizer->in_dquotes = 0;
    tokenizer->escaped = 0;
    tokenizer->is_first_word = 1;
    tokenizer->after_heredoc_op = 0;
    tokenizer->paren_depth_dparen = 0;
    tokenizer->paren_depth_arith = 0;
    tokenizer->in_backtick = 0;
    tokenizer->brace_depth_param = 0;

    if (!tokenizer->current_token || !tokenizer->tokens)
    {
        tokenizer_destroy(tokenizer);
        return NULL;
    }

    return tokenizer;
}

// Destroy tokenizer
void tokenizer_destroy(Tokenizer *tokenizer)
{
    if (!tokenizer)
        return;
    string_destroy(tokenizer->current_token);
    ptr_array_destroy(tokenizer->tokens);
    string_destroy(tokenizer->heredoc_delim);
    string_destroy(tokenizer->heredoc_content);
    free(tokenizer);
}

// Free function for Token
void token_free(void *element)
{
    if (!element)
        return;
    Token *token = (Token *)element;
    string_destroy(token->text);
    free(token);
}

// Create a new token
Token *token_create(String *text, TokenType type)
{
    Token *token = malloc(sizeof(Token));
    if (!token)
        return NULL;

    token->text = string_create_from(text);
    token->type = type;

    if (!token->text)
    {
        free(token);
        return NULL;
    }

    return token;
}

// Validate a name (POSIX: alphanumeric or underscore, not starting with digit)
int is_valid_name_zstring(const char *name)
{
    if (!name || !*name || isdigit(*name))
        return 0;
    for (const char *p = name; *p; p++)
    {
        if (!isalnum(*p) && *p != '_')
            return 0;
    }
    return 1;
}

int is_valid_name_string(const String *name)
{
    if (!name || string_is_empty(name))
        return 0;
    return is_valid_name_zstring(string_data(name));
}

// Helper: Check if a character is an operator start
static int is_operator_char(char c)
{
    return c == ';' || c == '|' || c == '&' || c == '<' || c == '>' || c == '(' || c == ')';
}

// Helper: Check if a string is a keyword
static int is_keyword_zstring(const char *text)
{
    const char *keywords[] = {
        "if", "then", "else", "elif", "fi",
        "while", "do", "done", "until",
        "for", "in", "case", "esac",
        "break", "continue", "return", NULL};
    for (int i = 0; keywords[i]; i++)
    {
        if (strcmp(text, keywords[i]) == 0)
            return 1;
    }
    return 0;
}

static int is_keyword_string(String *text)
{
    return is_keyword_zstring(string_data(text));
}

// Helper: Append a token to the tokenizer's array
static int add_token(Tokenizer *tokenizer, String *text, TokenType type)
{
    Token *token = token_create(text, type);
    if (!token)
        return -1;

    if (ptr_array_append(tokenizer->tokens, token) != 0)
    {
        token_free(token);
        return -1;
    }

    return 0;
}

// Helper: Process operator
static int process_operator(const char **p, String *current_token, TokenType *type)
{
    if (!p || !*p || !current_token)
        return -1;

    const char *start = *p;
    if (strncmp(start, "&&", 2) == 0)
    {
        *type = TOKEN_AND_IF;
        *p += 2;
    }
    else if (strncmp(start, "||", 2) == 0)
    {
        *type = TOKEN_OR_IF;
        *p += 2;
    }
    else if (strncmp(start, ";;", 2) == 0)
    {
        *type = TOKEN_DSEMI;
        *p += 2;
    }
    else if (strncmp(start, "<<-", 3) == 0)
    {
        *type = TOKEN_DLESSDASH;
        *p += 3;
    }
    else if (strncmp(start, "<<", 2) == 0)
    {
        *type = TOKEN_DLESS;
        *p += 2;
    }
    else if (strncmp(start, ">>", 2) == 0)
    {
        *type = TOKEN_DGREAT;
        *p += 2;
    }
    else if (strncmp(start, "<&", 2) == 0)
    {
        *type = TOKEN_LESSAND;
        *p += 2;
    }
    else if (strncmp(start, ">&", 2) == 0)
    {
        *type = TOKEN_GREATAND;
        *p += 2;
    }
    else if (strncmp(start, "<>", 2) == 0)
    {
        *type = TOKEN_LESSGREAT;
        *p += 2;
    }
    else if (strncmp(start, ">|", 2) == 0)
    {
        *type = TOKEN_CLOBBER;
        *p += 2;
    }
    else if (*start == '<' || *start == '>' || *start == '|' ||
             *start == '&' || *start == ';' || *start == '(' ||
             *start == ')')
    {
        *type = TOKEN_OPERATOR;
        *p += 1;
    }
    else
    {
        return -1;
    }

    size_t len = *p - start;
    char *op_str = malloc(len + 1);
    strncpy(op_str, start, len);
    op_str[len] = '\0';
    int ret = string_set(current_token, op_str);
    free (op_str);
    return ret;
}

// Helper: Process here-document delimiter and content
static TokenizerStatus process_heredoc(Tokenizer *tokenizer, const char **p, PtrArray *output_tokens, int (*get_line)(char *buf, int size))
{
    if (!tokenizer || !p || !*p || !output_tokens || !get_line)
        return TOKENIZER_FAILURE;

    // Add delimiter as TOKEN_HEREDOC_DELIM
    if (!string_is_empty(tokenizer->current_token))
    {
        if (add_token(tokenizer, tokenizer->current_token, TOKEN_HEREDOC_DELIM) != 0)
        {
            return TOKENIZER_FAILURE;
        }
        tokenizer->heredoc_delim = string_create_from(tokenizer->current_token);
        if (!tokenizer->heredoc_delim)
            return TOKENIZER_FAILURE;
        string_clear(tokenizer->current_token);
    }
    else
    {
        return TOKENIZER_FAILURE; // No delimiter
    }

    // Skip whitespace until newline
    while (*p && *p != '\n' && isspace(*p))
        p++;
    if (*p != '\n')
    {
        return TOKENIZER_FAILURE; // Expected newline after delimiter
    }
    p++; // Consume newline

    // Initialize here-document content
    if (!tokenizer->heredoc_content)
    {
        tokenizer->heredoc_content = string_create_empty(INITIAL_TOKEN_CAPACITY);
        if (!tokenizer->heredoc_content)
            return TOKENIZER_FAILURE;
    }

    // Move tokens to output
    for (size_t i = 0; i < ptr_array_size(tokenizer->tokens); i++)
    {
        ptr_array_append(output_tokens, ptr_array_get(tokenizer->tokens, i));
    }
    ptr_array_clear(tokenizer->tokens);

    return TOKENIZER_HEREDOC;
}

// Helper: Process here-document continuation
static TokenizerStatus process_heredoc_content(Tokenizer *tokenizer, const char *input, PtrArray *output_tokens, int (*get_line)(char *buf, int size))
{
    if (!tokenizer || !input || !output_tokens || !tokenizer->heredoc_delim)
        return TOKENIZER_FAILURE;

    size_t len = strlen(input);
    char *line = malloc (len + 1);
    strcpy(line, input);
    if (len > 0 && line[len - 1] == '\n')
    {
        line[len - 1] = '\0';
        len--;
    }

    // Check if line is the delimiter
    const char *delim = string_data(tokenizer->heredoc_delim);
    if (strcmp(line, delim) == 0)
    {
        // Add content as TOKEN_WORD
        if (!string_is_empty(tokenizer->heredoc_content))
        {
            if (add_token(tokenizer, tokenizer->heredoc_content, TOKEN_WORD) != 0)
            {
                free (line);
                return TOKENIZER_FAILURE;
            }
        }
        string_destroy(tokenizer->heredoc_content);
        tokenizer->heredoc_content = NULL;
        string_destroy(tokenizer->heredoc_delim);
        tokenizer->heredoc_delim = NULL;

        // Move tokens to output
        for (size_t i = 0; i < ptr_array_size(tokenizer->tokens); i++)
        {
            ptr_array_append(output_tokens, ptr_array_get(tokenizer->tokens, i));
        }
        ptr_array_clear(tokenizer->tokens);

        free (line);
        return TOKENIZER_SUCCESS;
    }

    // Append content line
    if (!string_is_empty(tokenizer->heredoc_content))
    {
        string_append(tokenizer->heredoc_content, "\n");
    }
    string_append(tokenizer->heredoc_content, line);

    free (line);
    return TOKENIZER_HEREDOC;
}

// Helper: Process command substitution $(...)
static int process_dparen(Tokenizer *tokenizer, const char **p, String *current_token)
{
    if (!tokenizer || !p || !*p || !current_token)
        return -1;

    if (tokenizer->paren_depth_dparen == 0)
    {
        string_append(current_token, "$(");
        *p += 2; // Skip $(
        tokenizer->paren_depth_dparen = 1;
    }
    else
    {
        (*p)++; // Skip (
        tokenizer->paren_depth_dparen++;
        string_append(current_token, "(");
    }

    while (*p)
    {
        if (tokenizer->escaped)
        {
            string_append(current_token, p);
            tokenizer->escaped = 0;
            p++;
            continue;
        }
        if (*p == '\\' && !tokenizer->in_quotes)
        {
            tokenizer->escaped = 1;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (*p == '\'' && !tokenizer->in_dquotes)
        {
            tokenizer->in_quotes = !tokenizer->in_quotes;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (*p == '"' && !tokenizer->in_quotes)
        {
            tokenizer->in_dquotes = !tokenizer->in_dquotes;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (!tokenizer->in_quotes && !tokenizer->in_dquotes)
        {
            if (*p == '(')
            {
                return process_dparen(tokenizer, p, current_token);
            }
            else if (*p == ')')
            {
                tokenizer->paren_depth_dparen--;
                string_append(current_token, p);
                p++;
                if (tokenizer->paren_depth_dparen == 0)
                {
                    return 0;
                }
                continue;
            }
        }
        char c[2] = {*p, '\0'};
        string_append(current_token, c);
        p++;
    }

    return 0; // Unclosed $(...)
}

// Helper: Process command substitution `...`
static int process_backtick(Tokenizer *tokenizer, const char **p, String *current_token)
{
    if (!tokenizer || !p || !*p || !current_token)
        return -1;

    if (!tokenizer->in_backtick)
    {
        string_append(current_token, "`");
        *p += 1; // Skip `
        tokenizer->in_backtick = 1;
    }
    else
    {
        string_append(current_token, "`");
        *p += 1; // Skip `
        tokenizer->in_backtick = 0;
        return 0;
    }

    while (*p)
    {
        if (tokenizer->escaped)
        {
            string_append(current_token, p);
            tokenizer->escaped = 0;
            p++;
            continue;
        }
        if (*p == '\\')
        {
            tokenizer->escaped = 1;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (*p == '\'' && !tokenizer->in_quotes)
        {
            tokenizer->in_quotes = !tokenizer->in_quotes;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (*p == '`' && !tokenizer->in_quotes)
        {
            return process_backtick(tokenizer, p, current_token);
        }
        char c[2] = {*p, '\0'};
        string_append(current_token, c);
        p++;
    }

    return 0; // Unclosed `...
}

// Helper: Process arithmetic expansion $((...))
static int process_arith(Tokenizer *tokenizer, const char **p, String *current_token)
{
    if (!tokenizer || !p || !*p || !current_token)
        return -1;

    if (tokenizer->paren_depth_arith == 0)
    {
        string_append(current_token, "$((");
        *p += 3; // Skip $((
        tokenizer->paren_depth_arith = 2;
    }
    else
    {
        (*p)++; // Skip (
        tokenizer->paren_depth_arith++;
        string_append(current_token, "(");
    }

    while (*p)
    {
        if (tokenizer->escaped)
        {
            string_append(current_token, p);
            tokenizer->escaped = 0;
            p++;
            continue;
        }
        if (*p == '\\' && !tokenizer->in_quotes)
        {
            tokenizer->escaped = 1;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (*p == '\'' && !tokenizer->in_dquotes)
        {
            tokenizer->in_quotes = !tokenizer->in_quotes;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (*p == '"' && !tokenizer->in_quotes)
        {
            tokenizer->in_dquotes = !tokenizer->in_dquotes;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (!tokenizer->in_quotes && !tokenizer->in_dquotes)
        {
            if (*p == '(')
            {
                return process_arith(tokenizer, p, current_token);
            }
            else if (*p == ')')
            {
                tokenizer->paren_depth_arith--;
                string_append(current_token, p);
                p++;
                if (tokenizer->paren_depth_arith == 1)
                {
                    continue; // Expect second )
                }
                if (tokenizer->paren_depth_arith == 0)
                {
                    return 0;
                }
            }
        }
        char c[2] = {*p, '\0'};
        string_append(current_token, c);
        p++;
    }

    return 0; // Unclosed $((...)
}

// Helper: Process parameter expansion ${...}
static int process_param(Tokenizer *tokenizer, const char **p, String *current_token)
{
    if (!tokenizer || !p || !*p || !current_token)
        return -1;

    if (tokenizer->brace_depth_param == 0)
    {
        string_append(current_token, "${");
        *p += 2; // Skip ${
        tokenizer->brace_depth_param = 1;
    }
    else
    {
        (*p)++; // Skip {
        tokenizer->brace_depth_param++;
        string_append(current_token, "{");
    }

    while (*p)
    {
        if (tokenizer->escaped)
        {
            string_append(current_token, p);
            tokenizer->escaped = 0;
            p++;
            continue;
        }
        if (*p == '\\' && !tokenizer->in_quotes)
        {
            tokenizer->escaped = 1;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (*p == '\'' && !tokenizer->in_dquotes)
        {
            tokenizer->in_quotes = !tokenizer->in_quotes;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (*p == '"' && !tokenizer->in_quotes)
        {
            tokenizer->in_dquotes = !tokenizer->in_dquotes;
            string_append(current_token, p);
            p++;
            continue;
        }
        if (!tokenizer->in_quotes && !tokenizer->in_dquotes)
        {
            if (*p == '{')
            {
                return process_param(tokenizer, p, current_token);
            }
            else if (*p == '}')
            {
                tokenizer->brace_depth_param--;
                string_append(current_token, p);
                p++;
                if (tokenizer->brace_depth_param == 0)
                {
                    return 0;
                }
                continue;
            }
        }
        char c[2] = {*p, '\0'};
        string_append(current_token, c);
        p++;
    }

    return 0; // Unclosed ${...
}

// Helper: Check if a string is a number
static int is_number_zstring(const char *text)
{
    if (!text || !*text)
        return 0;
    for (const char *p = text; *p; p++)
    {
        if (!isdigit(*p))
            return 0;
    }
    return 1;
}

static int is_number_string(String *text)
{
    return is_number_zstring(string_data(text));
}

// Main tokenize function
static TokenizerStatus tokenize_internal(Tokenizer *tokenizer, const char *input, PtrArray *output_tokens, int depth, AliasStore *alias_store, PtrArray *active_aliases, int (*get_line)(char *buf, int size))
{
    if (!tokenizer || !input || !output_tokens || !alias_store || !active_aliases || !get_line || depth > MAX_RECURSION_DEPTH)
    {
        return TOKENIZER_FAILURE;
    }

    // Handle here-document content if in heredoc state
    if (tokenizer->heredoc_delim)
    {
        return process_heredoc_content(tokenizer, input, output_tokens, get_line);
    }

    const char *p = input;

    while (*p)
    {
        if (tokenizer->escaped)
        {
            if (string_append(tokenizer->current_token, p) != 0)
            {
                return TOKENIZER_FAILURE;
            }
            tokenizer->escaped = 0;
            p++;
            continue;
        }

        if (*p == '\\' && !tokenizer->in_quotes)
        {
            if (*(p + 1) == '\0')
            {
                // Backslash at end of input
                tokenizer->escaped = 1;
                return TOKENIZER_LINE_CONTINUATION;
            }
            tokenizer->escaped = 1;
            p++;
            continue;
        }

        if (*p == '\'' && !tokenizer->in_dquotes)
        {
            tokenizer->in_quotes = !tokenizer->in_quotes;
            string_append(tokenizer->current_token, p);
            p++;
            continue;
        }

        if (*p == '"' && !tokenizer->in_quotes)
        {
            tokenizer->in_dquotes = !tokenizer->in_dquotes;
            string_append(tokenizer->current_token, p);
            p++;
            continue;
        }

        if (!tokenizer->in_quotes && !tokenizer->in_dquotes && isspace(*p))
        {
            if (!string_is_empty(tokenizer->current_token))
            {
                if (tokenizer->after_heredoc_op)
                {
                    // Heredoc delimiter; process content
                    TokenizerStatus status = process_heredoc(tokenizer, &p, output_tokens, get_line);
                    if (status != TOKENIZER_HEREDOC)
                    {
                        return status;
                    }
                    tokenizer->after_heredoc_op = 0;
                    tokenizer->is_first_word = 0;
                }
                else if (tokenizer->is_first_word &&
                         alias_exists_string(alias_store, tokenizer->current_token) &&
                         !alias_is_active_string(tokenizer->current_token, active_aliases))
                {
                    // Handle alias substitution
                    String *alias_value = alias_get_string(alias_store, tokenizer->current_token);
                    if (!alias_value)
                    {
                        return TOKENIZER_FAILURE;
                    }

                    String *alias_name = string_create_from(tokenizer->current_token);
                    if (!alias_name || ptr_array_append(active_aliases, alias_name) != 0)
                    {
                        string_destroy(alias_name);
                        return TOKENIZER_FAILURE;
                    }

                    PtrArray *new_tokens = ptr_array_create_with_free(token_free);
                    if (!new_tokens)
                    {
                        return TOKENIZER_FAILURE;
                    }

                    Tokenizer *alias_tokenizer = tokenizer_create();
                    if (!alias_tokenizer)
                    {
                        ptr_array_destroy(new_tokens);
                        return TOKENIZER_FAILURE;
                    }

                    TokenizerStatus ret = tokenize_internal(alias_tokenizer, string_data(alias_value),
                                                            new_tokens, depth + 1, alias_store,
                                                            active_aliases, get_line);
                    if (ret != TOKENIZER_SUCCESS)
                    {
                        tokenizer_destroy(alias_tokenizer);
                        ptr_array_destroy(new_tokens);
                        return ret;
                    }

                    for (size_t i = 0; i < ptr_array_size(new_tokens); i++)
                    {
                        ptr_array_append(tokenizer->tokens, ptr_array_get(new_tokens, i));
                    }
                    ptr_array_clear(new_tokens);
                    ptr_array_destroy(new_tokens);
                    tokenizer_destroy(alias_tokenizer);
                    tokenizer->is_first_word = 0;
                }
                else
                {
                    // Regular word
                    if (add_token(tokenizer, tokenizer->current_token, TOKEN_WORD) != 0)
                    {
                        return TOKENIZER_FAILURE;
                    }
                    tokenizer->is_first_word = 0;
                }

                string_clear(tokenizer->current_token);
            }
            p++;
            continue;
        }

        if (!tokenizer->in_quotes && !tokenizer->in_dquotes && *p == '#')
        {
            if (!string_is_empty(tokenizer->current_token))
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_WORD) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }

            // Process comment
            const char *start = p;
            while (*p && *p != '\n')
                p++;
            size_t len = p - start;
            char *comment_str = malloc (len + 1);
            strncpy(comment_str, start, len);
            comment_str[len] = '\0';

            if (string_set(tokenizer->current_token, comment_str) != 0 ||
                add_token(tokenizer, tokenizer->current_token, TOKEN_COMMENT) != 0)
            {
                free (comment_str);
                return TOKENIZER_FAILURE;
            }

            free (comment_str);
            string_clear(tokenizer->current_token);
            continue;
        }

        if (!tokenizer->in_quotes && !tokenizer->in_dquotes && strncmp(p, "$(", 2) == 0)
        {
            if (!string_is_empty(tokenizer->current_token))
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_WORD) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }

            // Process $(...)
            if (process_dparen(tokenizer, &p, tokenizer->current_token) != 0)
            {
                return TOKENIZER_FAILURE;
            }

            if (tokenizer->paren_depth_dparen == 0)
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_DPAREN) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }
            continue;
        }

        if (!tokenizer->in_quotes && !tokenizer->in_dquotes && strncmp(p, "$((", 3) == 0)
        {
            if (!string_is_empty(tokenizer->current_token))
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_WORD) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }

            // Process $((...)
            if (process_arith(tokenizer, &p, tokenizer->current_token) != 0)
            {
                return TOKENIZER_FAILURE;
            }

            if (tokenizer->paren_depth_arith == 0)
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_ARITH) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }
            continue;
        }

        if (!tokenizer->in_quotes && !tokenizer->in_dquotes && strncmp(p, "${", 2) == 0)
        {
            if (!string_is_empty(tokenizer->current_token))
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_WORD) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }

            // Process ${...
            if (process_param(tokenizer, &p, tokenizer->current_token) != 0)
            {
                return TOKENIZER_FAILURE;
            }

            if (tokenizer->brace_depth_param == 0)
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_PARAM) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }
            continue;
        }

        if (!tokenizer->in_quotes && !tokenizer->in_dquotes && *p == '`')
        {
            if (!string_is_empty(tokenizer->current_token))
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_WORD) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }

            // Process `...`
            if (process_backtick(tokenizer, &p, tokenizer->current_token) != 0)
            {
                return TOKENIZER_FAILURE;
            }

            if (!tokenizer->in_backtick)
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_BACKTICK) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }
            continue;
        }

        if (!tokenizer->in_quotes && !tokenizer->in_dquotes && is_operator_char(*p))
        {
            if (!string_is_empty(tokenizer->current_token))
            {
                if (add_token(tokenizer, tokenizer->current_token, TOKEN_WORD) != 0)
                {
                    return TOKENIZER_FAILURE;
                }
                string_clear(tokenizer->current_token);
            }

            // Process operator
            TokenType op_type;
            if (process_operator(&p, tokenizer->current_token, &op_type) != 0)
            {
                return TOKENIZER_FAILURE;
            }

            if (add_token(tokenizer, tokenizer->current_token, op_type) != 0)
            {
                return TOKENIZER_FAILURE;
            }

            string_clear(tokenizer->current_token);
            tokenizer->after_heredoc_op = (op_type == TOKEN_DLESS || op_type == TOKEN_DLESSDASH);
            tokenizer->is_first_word = (op_type == TOKEN_OPERATOR ||
                                        op_type == TOKEN_AND_IF ||
                                        op_type == TOKEN_OR_IF ||
                                        op_type == TOKEN_DSEMI);
            continue;
        }

        if (!tokenizer->in_quotes && !tokenizer->in_dquotes && *p == '~' && string_is_empty(tokenizer->current_token))
        {
            // Handle tilde
            const char *start = p;
            p++;
            while (*p && (isalnum(*p) || *p == '_' || *p == '+' || *p == '-'))
                p++;

            size_t len = p - start;
            char *tilde_str = malloc (len + 1);
            strncpy(tilde_str, start, len);
            tilde_str[len] = '\0';

            if (string_set(tokenizer->current_token, tilde_str) != 0 ||
                add_token(tokenizer, tokenizer->current_token, TOKEN_TILDE) != 0)
            {
                free (tilde_str);
                return TOKENIZER_FAILURE;
            }

            free (tilde_str);
            string_clear(tokenizer->current_token);
            continue;
        }

        if (!tokenizer->in_quotes && !tokenizer->in_dquotes && *p == '\n')
        {
            if (!string_is_empty(tokenizer->current_token))
            {
                if (tokenizer->after_heredoc_op)
                {
                    // Heredoc delimiter; process content
                    TokenizerStatus status = process_heredoc(tokenizer, &p, output_tokens, get_line);
                    if (status != TOKENIZER_HEREDOC)
                    {
                        return status;
                    }
                    tokenizer->after_heredoc_op = 0;
                }
                else
                {
                    if (add_token(tokenizer, tokenizer->current_token, TOKEN_WORD) != 0)
                    {
                        return TOKENIZER_FAILURE;
                    }
                }
                string_clear(tokenizer->current_token);
            }

            if (add_token(tokenizer, tokenizer->current_token, TOKEN_NEWLINE) != 0)
            {
                return TOKENIZER_FAILURE;
            }

            tokenizer->is_first_word = 1;
            p++;
            continue;
        }

        // Append character
        char c[2] = {*p, '\0'};
        if (string_append(tokenizer->current_token, c) != 0)
        {
            return TOKENIZER_FAILURE;
        }
        p++;
    }

    // Handle end of input
    if (tokenizer->escaped)
    {
        return TOKENIZER_LINE_CONTINUATION;
    }

    if (tokenizer->in_quotes || tokenizer->in_dquotes || tokenizer->paren_depth_dparen > 0 ||
        tokenizer->paren_depth_arith > 0 || tokenizer->in_backtick || tokenizer->brace_depth_param > 0)
    {
        return TOKENIZER_LINE_CONTINUATION;
    }

    if (!string_is_empty(tokenizer->current_token))
    {
        if (tokenizer->after_heredoc_op)
        {
            // Heredoc delimiter; process content
            const char *end = p;
            TokenizerStatus status = process_heredoc(tokenizer, &end, output_tokens, get_line);
            if (status != TOKENIZER_HEREDOC)
            {
                return status;
            }
            tokenizer->after_heredoc_op = 0;
        }
        else if (tokenizer->is_first_word &&
                 alias_exists_string(alias_store, tokenizer->current_token) &&
                 !alias_is_active_string(tokenizer->current_token, active_aliases))
        {
            String *alias_value = alias_get_string(alias_store, tokenizer->current_token);
            if (!alias_value)
            {
                return TOKENIZER_FAILURE;
            }

            String *alias_name = string_create_from(tokenizer->current_token);
            if (!alias_name || ptr_array_append(active_aliases, alias_name) != 0)
            {
                string_destroy(alias_name);
                return TOKENIZER_FAILURE;
            }

            PtrArray *new_tokens = ptr_array_create_with_free(token_free);
            if (!new_tokens)
            {
                return TOKENIZER_FAILURE;
            }

            Tokenizer *alias_tokenizer = tokenizer_create();
            if (!alias_tokenizer)
            {
                ptr_array_destroy(new_tokens);
                return TOKENIZER_FAILURE;
            }

            TokenizerStatus ret = tokenize_internal(alias_tokenizer, string_data(alias_value),
                                                    new_tokens, depth + 1, alias_store,
                                                    active_aliases, get_line);
            if (ret != TOKENIZER_SUCCESS)
            {
                tokenizer_destroy(alias_tokenizer);
                ptr_array_destroy(new_tokens);
                return ret;
            }

            for (size_t i = 0; i < ptr_array_size(new_tokens); i++)
            {
                ptr_array_append(tokenizer->tokens, ptr_array_get(new_tokens, i));
            }
            ptr_array_clear(new_tokens);
            ptr_array_destroy(new_tokens);
            tokenizer_destroy(alias_tokenizer);
        }
        else
        {
            if (add_token(tokenizer, tokenizer->current_token, TOKEN_WORD) != 0)
            {
                return TOKENIZER_FAILURE;
            }
        }
        string_clear(tokenizer->current_token);
    }

    // Post-process tokens
    for (size_t i = 0; i < ptr_array_size(tokenizer->tokens); i++)
    {
        Token *token = ptr_array_get(tokenizer->tokens, i);
        if (token->type == TOKEN_WORD)
        {
            if (is_keyword_string(token->text))
            {
                token->type = TOKEN_KEYWORD;
            }
            else if (i + 1 < ptr_array_size(tokenizer->tokens))
            {
                Token *next = ptr_array_get(tokenizer->tokens, i + 1);
                if ((next->type == TOKEN_OPERATOR &&
                     (strcmp(string_data(next->text), ">") == 0 ||
                      strcmp(string_data(next->text), "<") == 0)) ||
                    next->type == TOKEN_DLESS || next->type == TOKEN_DGREAT ||
                    next->type == TOKEN_DLESSDASH)
                {
                    if (is_number_string(token->text))
                    {
                        token->type = TOKEN_IO_NUMBER;
                    }
                }
            }
            // Detect assignments (var=value)
            const char *text = string_data(token->text);
            if (strchr(text, '=') && !strchr(text, ' ') && is_valid_name_zstring(strtok((char *)text, "=")))
            {
                token->type = TOKEN_ASSIGNMENT;
            }
        }
    }

    // Move tokens to output
    for (size_t i = 0; i < ptr_array_size(tokenizer->tokens); i++)
    {
        ptr_array_append(output_tokens, ptr_array_get(tokenizer->tokens, i));
    }
    ptr_array_clear(tokenizer->tokens);

    return TOKENIZER_SUCCESS;
}

// Public tokenize functions
TokenizerStatus tokenize_zstring(Tokenizer *tokenizer, const char *input, PtrArray *tokens, AliasStore *alias_store, int (*get_line)(char *buf, int size))
{
    if (!tokenizer || !input || !tokens || !alias_store || !get_line)
        return TOKENIZER_FAILURE;

    PtrArray *active_aliases = ptr_array_create_with_free(string_destroy);
    if (!active_aliases)
        return TOKENIZER_FAILURE;

    TokenizerStatus result = tokenize_internal(tokenizer, input, tokens, 0, alias_store, active_aliases, get_line);

    ptr_array_destroy(active_aliases);
    return result;
}

TokenizerStatus tokenize_string(Tokenizer *tokenizer, String *input, PtrArray *tokens, AliasStore *alias_store, int (*get_line)(char *buf, int size))
{
    if (!input || !tokenizer || !tokens || !alias_store || !get_line)
        return TOKENIZER_FAILURE;
    return tokenize_zstring(tokenizer, string_data(input), tokens, alias_store, get_line);
}
