#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "executor.h"
#include "builtins.h"
#include "tokenizer.h"
#include "parser.h"
#include "variables.h"
#include "function_store.h"
#include "trap_store.h"
#include "string.h"

int main(int argc, char **argv) {
    // Initialize components
    VariableStore *vars = variable_store_create();
    if (!vars) {
        fprintf(stderr, "sh: failed to create variable store\n");
        return 1;
    }

    Tokenizer *tokenizer = tokenizer_create();
    if (!tokenizer) {
        variable_store_destroy(vars);
        fprintf(stderr, "sh: failed to create tokenizer\n");
        return 1;
    }

    AliasStore *alias_store = alias_store_create();
    if (!alias_store) {
        tokenizer_destroy(tokenizer);
        variable_store_destroy(vars);
        fprintf(stderr, "sh: failed to create alias store\n");
        return 1;
    }

    FunctionStore *func_store = function_store_create();
    if (!func_store) {
        alias_store_destroy(alias_store);
        tokenizer_destroy(tokenizer);
        variable_store_destroy(vars);
        fprintf(stderr, "sh: failed to create function store\n");
        return 1;
    }

    TrapStore *trap_store = trap_store_create();
    if (!trap_store) {
        function_store_destroy(func_store);
        alias_store_destroy(alias_store);
        tokenizer_destroy(tokenizer);
        variable_store_destroy(vars);
        fprintf(stderr, "sh: failed to create trap store\n");
        return 1;
    }

    Executor *exec = executor_create(vars, tokenizer, alias_store, func_store);
    if (!exec) {
        trap_store_destroy(trap_store);
        function_store_destroy(func_store);
        alias_store_destroy(alias_store);
        tokenizer_destroy(tokenizer);
        variable_store_destroy(vars);
        fprintf(stderr, "sh: failed to create executor\n");
        return 1;
    }
    exec->trap_store = trap_store;

    // Set up environment
    extern char **environ;
    for (int i = 0; environ[i]; i++) {
        char *eq = strchr(environ[i], '=');
        if (eq) {
            *eq = '\0';
            variable_store_set_variable(vars, environ[i], eq + 1);
            variable_store_export_variable(vars, environ[i]);
            *eq = '=';
        }
    }

    // Set shell name
    char *shell_name = (argc > 0) ? argv[0] : "sh";
    variable_store_set_variable(vars, "0", shell_name);

    // Set PWD
    char *pwd = getcwd(NULL, 0);
    if (pwd) {
        variable_store_set_variable(vars, "PWD", pwd);
        free(pwd);
    } else {
        variable_store_set_variable(vars, "PWD", "");
    }

    // Set default PS1, PS2
    variable_store_set_variable(vars, "PS1", "$ ");
    variable_store_set_variable(vars, "PS2", "> ");

    // Handle command-line arguments
    bool is_interactive = (argc == 1 && isatty(STDIN_FILENO));
    exec->is_interactive = is_interactive;
    int exit_status = 0;

    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        // Non-interactive: -c "command"
        if (argc < 3) {
            fprintf(stderr, "sh: -c: option requires an argument\n");
            exit_status = 2;
        } else {
            // Set positional parameters
            for (int i = 3; i < argc; i++) {
                ptr_array_append(vars->positional_params, strdup(argv[i]));
            }

            String *input = string_create();
            string_append_zstring(input, argv[2]);
            PtrArray *tokens = ptr_array_create();
            if (tokenize_string(tokenizer, input, tokens, alias_store, NULL) == TOKENIZER_SUCCESS) {
                Parser *parser = parser_create(tokenizer, alias_store);
                ASTNode *ast = NULL;
                if (parser_apply_tokens(parser, tokens, &ast) == PARSE_COMPLETE && ast) {
                    ExecStatus ret = executor_run(exec, ast);
                    exit_status = exec->last_status;
                    ast_node_destroy(ast);
                } else {
                    fprintf(stderr, "sh: parse error\n");
                    exit_status = 2;
                }
                parser_destroy(parser);
            } else {
                fprintf(stderr, "sh: tokenization error\n");
                exit_status = 2;
            }
            ptr_array_destroy_with_elements(tokens, token_free);
            string_destroy(input);
        }
    } else if (argc > 1) {
        // Non-interactive: script file
        FILE *fp = fopen(argv[1], "r");
        if (!fp) {
            fprintf(stderr, "sh: %s: %s\n", argv[1], strerror(errno));
            exit_status = 1;
        } else {
            // Set $0 to script name
            variable_store_set_variable(vars, "0", argv[1]);
            // Set positional parameters
            for (int i = 2; i < argc; i++) {
                ptr_array_append(vars->positional_params, strdup(argv[i]));
            }

            String *script = string_create();
            char buf[1024];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                string_append_buffer(script, buf, n);
            }
            fclose(fp);

            PtrArray *tokens = ptr_array_create();
            if (tokenize_string(tokenizer, script, tokens, alias_store, NULL) == TOKENIZER_SUCCESS) {
                Parser *parser = parser_create(tokenizer, alias_store);
                ASTNode *ast = NULL;
                if (parser_apply_tokens(parser, tokens, &ast) == PARSE_COMPLETE && ast) {
                    ExecStatus ret = executor_run(exec, ast);
                    exit_status = exec->last_status;
                    ast_node_destroy(ast);
                } else {
                    fprintf(stderr, "sh: %s: parse error\n", argv[1]);
                    exit_status = 2;
                }
                parser_destroy(parser);
            } else {
                fprintf(stderr, "sh: %s: tokenization error\n", argv[1]);
                exit_status = 2;
            }
            ptr_array_destroy_with_elements(tokens, token_free);
            string_destroy(script);
        }
    } else {
        // Interactive mode
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);

        String *input_buffer = string_create();
        while (1) {
            // Print PS1 for new command or PS2 for continuation
            const char *prompt = string_is_empty(input_buffer) ?
                                 variable_store_get_variable(vars, "PS1") :
                                 variable_store_get_variable(vars, "PS2");
            if (!prompt) prompt = string_is_empty(input_buffer) ? "$ " : "> ";
            printf("%s", prompt);
            fflush(stdout);

            char buf[1024];
            if (!fgets(buf, sizeof(buf), stdin)) {
                if (!string_is_empty(input_buffer)) {
                    fprintf(stderr, "sh: unexpected EOF\n");
                    executor_set_status(exec, 2);
                    string_clear(input_buffer);
                } else {
                    // Clean EOF, exit
                    break;
                }
            } else {
                string_append_zstring(input_buffer, buf);
            }

            PtrArray *tokens = ptr_array_create();
            TokenizerStatus status = tokenize_string(tokenizer, input_buffer, tokens, alias_store, NULL);

            if (status == TOKENIZER_SUCCESS) {
                Parser *parser = parser_create(tokenizer, alias_store);
                ASTNode *ast = NULL;
                if (parser_apply_tokens(parser, tokens, &ast) == PARSE_COMPLETE && ast) {
                    ExecStatus ret = executor_run(exec, ast);
                    exit_status = exec->last_status;
                    ast_node_destroy(ast);
                } else {
                    fprintf(stderr, "sh: parse error\n");
                    executor_set_status(exec, 2);
                }
                parser_destroy(parser);
                string_clear(input_buffer);
            } else if (status == TOKENIZER_FAILURE) {
                fprintf(stderr, "sh: tokenization error\n");
                executor_set_status(exec, 2);
                string_clear(input_buffer);
            } else {
                // TOKENIZER_LINE_CONTINUATION or TOKENIZER_HEREDOC
                // Keep tokens for context, continue buffering
                ptr_array_destroy_with_elements(tokens, token_free);
                continue;
            }

            ptr_array_destroy_with_elements(tokens, token_free);
        }
        string_destroy(input_buffer);
    }

    // Cleanup
    executor_destroy(exec);
    trap_store_destroy(trap_store);
    function_store_destroy(func_store);
    alias_store_destroy(alias_store);
    tokenizer_destroy(tokenizer);
    variable_store_destroy(vars);

    return exit_status;
}
