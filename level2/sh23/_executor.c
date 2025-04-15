#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "executor.h"
#include "string.h"

// Global executor for signal handler (set in executor_create)
static Executor *global_executor = NULL;

// TrapStore management
static TrapStore *trap_store_create(void) {
    TrapStore *store = malloc(sizeof(TrapStore));
    if (!store) return NULL;
    store->traps = ptr_array_create();
    if (!store->traps) {
        free(store);
        return NULL;
    }
    return store;
}

static void trap_store_destroy(TrapStore *store) {
    if (!store) return;
    for (int i = 0; i < store->traps->len; i++) {
        Trap *trap = store->traps->data[i];
        string_destroy(trap->action);
        free(trap);
    }
    ptr_array_destroy(store->traps);
    free(store);
}

static void trap_store_set(TrapStore *store, int signal, const char *action) {
    // Find existing trap
    for (int i = 0; i < store->traps->len; i++) {
        Trap *trap = store->traps->data[i];
        if (trap->signal == signal) {
            string_destroy(trap->action);
            if (action == NULL) {
                trap->action = NULL; // Default
            } else if (strcmp(action, "-") == 0) {
                trap->action = string_create_from("-");
            } else {
                trap->action = string_create_from(action);
            }
            return;
        }
    }
    // New trap
    Trap *trap = malloc(sizeof(Trap));
    if (!trap) return;
    trap->signal = signal;
    if (action == NULL) {
        trap->action = NULL;
    } else if (strcmp(action, "-") == 0) {
        trap->action = string_create_from("-");
    } else {
        trap->action = string_create_from(action);
    }
    if (action && trap->action == NULL) {
        free(trap);
        return;
    }
    ptr_array_append(store->traps, trap);
}

static Trap *trap_store_get(TrapStore *store, int signal) {
    for (int i = 0; i < store->traps->len; i++) {
        Trap *trap = store->traps->data[i];
        if (trap->signal == signal) {
            return trap;
        }
    }
    return NULL;
}

// Signal handler
static void signal_handler(int sig) {
    if (!global_executor) return;
    Trap *trap = trap_store_get(global_executor->trap_store, sig);
    if (!trap || !trap->action || strcmp(string_cstr(trap->action), "-") == 0) {
        return; // Ignore or no action
    }
    // Execute command in current environment
    String *action = string_create_from(string_cstr(trap->action));
    PtrArray *tokens = ptr_array_create();
    Tokenizer *tokenizer = tokenizer_create();
    if (tokenize_string(tokenizer, action, tokens, global_executor->alias_store, NULL) != TOKENIZER_SUCCESS) {
        ptr_array_destroy_with_elements(tokens, token_free);
        tokenizer_destroy(tokenizer);
        string_destroy(action);
        return;
    }
    Parser *parser = parser_create(tokenizer, global_executor->alias_store);
    ASTNode *ast = NULL;
    ParseStatus parse_status = parser_apply_tokens(parser, tokens, &ast);
    ptr_array_destroy_with_elements(tokens, token_free);
    if (parse_status != PARSE_COMPLETE || !ast) {
        parser_destroy(parser);
        string_destroy(action);
        return;
    }
    executor_run(global_executor, ast);
    // Note: AST cleanup is parser’s responsibility
    parser_destroy(parser);
    string_destroy(action);
}

// Create executor
Executor *executor_create(VariableStore *vars, Tokenizer *tokenizer, AliasStore *alias_store, FunctionStore *func_store) {
    Executor *exec = malloc(sizeof(Executor));
    if (!exec) return NULL;
    exec->vars = vars;
    exec->tokenizer = tokenizer;
    exec->alias_store = alias_store;
    exec->func_store = func_store;
    exec->trap_store = trap_store_create();
    if (!exec->trap_store) {
        free(exec);
        return NULL;
    }
    exec->last_status = 0;
    exec->last_bg_pid = 0;
    exec->pipe_fds[0] = -1;
    exec->pipe_fds[1] = -1;
    exec->saved_fds[0] = dup(STDIN_FILENO);
    exec->saved_fds[1] = dup(STDOUT_FILENO);
    exec->saved_fds[2] = dup(STDERR_FILENO);
    exec->in_subshell = 0;
    exec->break_count = 0;
    exec->continue_count = 0;
    exec->function_name = NULL;
    exec->loop_depth = 0;
    exec->function_depth = 0;
    global_executor = exec; // For signal handler
    return exec;
}

// Destroy executor
void executor_destroy(Executor *exec) {
    if (!exec) return;
    for (int i = 0; i < 3; i++) {
        if (exec->saved_fds[i] != -1) close(exec->saved_fds[i]);
    }
    free(exec->function_name);
    trap_store_destroy(exec->trap_store);
    // Note: func_store is owned by caller
    global_executor = NULL;
    free(exec);
}

// Set and get status
void executor_set_status(Executor *exec, int status) {
    exec->last_status = status;
    char status_str[16];
    snprintf(status_str, sizeof(status_str), "%d", status);
    variable_store_set_status(exec->vars, status);
}

int executor_get_status(Executor *exec) {
    return exec->last_status;
}

// Apply a single redirect
static int apply_redirect(Redirect *redir) {
    int fd = redir->io_number ? atoi(redir->io_number) : (redir->operation == LESS ? STDIN_FILENO : STDOUT_FILENO);
    int new_fd = -1;

    switch (redir->operation) {
        case LESS:
            new_fd = open(redir->filename, O_RDONLY);
            break;
        case GREAT:
            new_fd = open(redir->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            break;
        case DGREAT:
            new_fd = open(redir->filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
            break;
        case DLESS: {
            int pipefd[2];
            if (pipe(pipefd) == -1) return -1;
            write(pipefd[1], redir->heredoc_content, strlen(redir->heredoc_content));
            close(pipefd[1]);
            new_fd = pipefd[0];
            break;
        }
        case DLESSDASH:
            return apply_redirect(redir);
        case LESSAND:
            new_fd = atoi(redir->filename);
            break;
        case GREATAND:
            new_fd = atoi(redir->filename);
            break;
        case LESSGREAT:
            new_fd = open(redir->filename, O_RDWR | O_CREAT, 0644);
            break;
        case CLOBBER:
            new_fd = open(redir->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            break;
        default:
            return -1;
    }

    if (new_fd == -1) return -1;
    if (dup2(new_fd, fd) == -1) {
        close(new_fd);
        return -1;
    }
    if (redir->operation != LESSAND && redir->operation != GREATAND) {
        close(new_fd);
    }
    return 0;
}

// Trim trailing newlines for command substitution
static void trim_trailing_newlines(String *str) {
    char *buf = string_cstr(str);
    size_t len = strlen(buf);
    while (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        len--;
    }
    string_truncate(str, len);
}

// Expand a string with expansions
static char *expand_string(Executor *exec, char *input, Expansion **expansions, int expansion_count) {
    if (!expansions || expansion_count == 0) return strdup(input);

    String *result = string_create();

    for (int i = 0; i < expansion_count; i++) {
        Expansion *exp = expansions[i];
        switch (exp->type) {
            case EXPANSION_PARAMETER: {
                const char *value = variable_store_get_variable(exec->vars, exp->data.parameter.name);
                if (value) string_append_zstring(result, value);
                break;
            }
            case EXPANSION_SPECIAL: {
                char buf[32];
                switch (exp->data.special.param) {
                    case SPECIAL_QUESTION:
                        snprintf(buf, sizeof(buf), "%d", exec->last_status);
                        string_append_zstring(result, buf);
                        break;
                    case SPECIAL_BANG:
                        if (exec->last_bg_pid) {
                            snprintf(buf, sizeof(buf), "%ld", (long)exec->last_bg_pid);
                            string_append_zstring(result, buf);
                        }
                        break;
                    default:
                        break;
                }
                break;
            }
            case EXPANSION_COMMAND: {
                int pipefd[2];
                if (pipe(pipefd) == -1) {
                    fprintf(stderr, "command substitution: pipe failed: %s\n", strerror(errno));
                    break;
                }

                pid_t pid = fork();
                if (pid == -1) {
                    fprintf(stderr, "command substitution: fork failed: %s\n", strerror(errno));
                    close(pipefd[0]);
                    close(pipefd[1]);
                    break;
                }

                if (pid == 0) {
                    close(pipefd[0]);
                    if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                        fprintf(stderr, "command substitution: dup2 failed: %s\n", strerror(errno));
                        exit(1);
                    }
                    close(pipefd[1]);
                    ExecStatus ret = executor_run(exec, exp->data.command.command);
                    exit(ret == EXEC_SUCCESS || ret == EXEC_RETURN ? exec->last_status : 1);
                }

                close(pipefd[1]);
                String *output = string_create();
                char buf[1024];
                ssize_t n;
                while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
                    string_append_buffer(output, buf, n);
                }
                close(pipefd[0]);

                int status;
                waitpid(pid, &status, 0);

                trim_trailing_newlines(output);
                string_append_string(result, output);
                string_destroy(output);
                break;
            }
            default:
                break;
        }
    }

    char *expanded = strdup(string_cstr(result));
    string_destroy(result);
    return expanded;
}

// Execute a simple command
static ExecStatus exec_simple_command(Executor *exec, ASTNode *node) {
    ASTNode *cmd = node;
    char **argv = NULL;
    int argc = 0;

    // Handle assignments
    for (int i = 0; i < cmd->data.simple_command.prefix_count; i++) {
        variable_store_set_variable(exec->vars, 
            cmd->data.simple_command.prefix[i].name,
            cmd->data.simple_command.prefix[i].value);
    }

    // Build argument list
    if (cmd->data.simple_command.command) {
        argv = realloc(argv, sizeof(char *) * (argc + 1));
        argv[argc++] = expand_string(exec, cmd->data.simple_command.command,
            cmd->data.simple_command.expansions,
            cmd->data.simple_command.expansion_count);
    }
    for (int i = 0; i < cmd->data.simple_command.suffix_count; i++) {
        argv = realloc(argv, sizeof(char *) * (argc + 1));
        argv[argc++] = expand_string(exec, cmd->data.simple_command.suffix[i],
            cmd->data.simple_command.expansions,
            cmd->data.simple_command.expansion_count);
    }
    if (argv) {
        argv = realloc(argv, sizeof(char *) * (argc + 1));
        argv[argc] = NULL;
    }

    // Apply redirects
    Redirect *redir = cmd->data.simple_command.redirects;
    while (redir) {
        if (apply_redirect(redir) == -1) {
            executor_set_status(exec, 1);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_FAILURE;
        }
        redir = redir->next;
    }

    // Check for builtins
    if (argv && argv[0]) {
        // : (true)
        if (strcmp(argv[0], ":") == 0) {
            executor_set_status(exec, 0);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_SUCCESS;
        }
        // . (dot/source)
        if (strcmp(argv[0], ".") == 0) {
            if (argc < 2) {
                fprintf(stderr, ".: filename argument required\n");
                executor_set_status(exec, 2);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            FILE *fp = fopen(argv[1], "r");
            if (!fp) {
                fprintf(stderr, ".: %s: %s\n", argv[1], strerror(errno));
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            String *script = string_create();
            char buf[1024];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                string_append_buffer(script, buf, n);
            }
            fclose(fp);
            PtrArray *tokens = ptr_array_create();
            Tokenizer *tokenizer = tokenizer_create();
            if (tokenize_string(tokenizer, script, tokens, exec->alias_store, NULL) != TOKENIZER_SUCCESS) {
                fprintf(stderr, ".: %s: tokenization failed\n", argv[1]);
                ptr_array_destroy_with_elements(tokens, token_free);
                tokenizer_destroy(tokenizer);
                string_destroy(script);
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            Parser *parser = parser_create(tokenizer, exec->alias_store);
            ASTNode *ast = NULL;
            ParseStatus parse_status = parser_apply_tokens(parser, tokens, &ast);
            ptr_array_destroy_with_elements(tokens, token_free);
            if (parse_status != PARSE_COMPLETE || !ast) {
                fprintf(stderr, ".: %s: parse failed\n", argv[1]);
                parser_destroy(parser);
                string_destroy(script);
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            ExecStatus ret = executor_run(exec, ast);
            parser_destroy(parser);
            string_destroy(script);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return ret;
        }
        // eval
        if (strcmp(argv[0], "eval") == 0) {
            String *input = string_create();
            for (int i = 1; i < argc; i++) {
                string_append_zstring(input, argv[i]);
                if (i < argc - 1) string_append_zstring(input, " ");
            }
            PtrArray *tokens = ptr_array_create();
            Tokenizer *tokenizer = tokenizer_create();
            if (tokenize_string(tokenizer, input, tokens, exec->alias_store, NULL) != TOKENIZER_SUCCESS) {
                ptr_array_destroy_with_elements(tokens, token_free);
                tokenizer_destroy(tokenizer);
                string_destroy(input);
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            Parser *parser = parser_create(tokenizer, exec->alias_store);
            ASTNode *ast = NULL;
            ParseStatus parse_status = parser_apply_tokens(parser, tokens, &ast);
            ptr_array_destroy_with_elements(tokens, token_free);
            if (parse_status != PARSE_COMPLETE || !ast) {
                parser_destroy(parser);
                string_destroy(input);
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            ExecStatus ret = executor_run(exec, ast);
            parser_destroy(parser);
            string_destroy(input);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return ret;
        }
        // exec
        if (strcmp(argv[0], "exec") == 0) {
            if (argc == 1) {
                executor_set_status(exec, 0);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_SUCCESS;
            }
            execvp(argv[1], &argv[1]);
            fprintf(stderr, "exec: %s: %s\n", argv[1], strerror(errno));
            executor_set_status(exec, 1);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_FAILURE;
        }
        // exit
        if (strcmp(argv[0], "exit") == 0) {
            int status = exec->last_status;
            if (argc > 1) {
                char *endptr;
                long n = strtol(argv[1], &endptr, 10);
                if (*endptr != '\0') {
                    fprintf(stderr, "exit: %s: numeric argument required\n", argv[1]);
                    status = 2;
                } else {
                    status = (int)n;
                }
            }
            if (argc > 2) {
                fprintf(stderr, "exit: too many arguments\n");
                status = 2;
            }
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            exit(status);
        }
        // export
        if (strcmp(argv[0], "export") == 0) {
            if (argc == 1) {
                variable_store_dump_variables(exec->vars);
                executor_set_status(exec, 0);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_SUCCESS;
            }
            int status = 0;
            for (int i = 1; i < argc; i++) {
                char *eq = strchr(argv[i], '=');
                if (eq) {
                    *eq = '\0';
                    if (!is_valid_name_zstring(argv[i])) {
                        fprintf(stderr, "export: %s: not a valid identifier\n", argv[i]);
                        status = 1;
                        continue;
                    }
                    variable_store_set_variable(exec->vars, argv[i], eq + 1);
                    variable_store_export_variable(exec->vars, argv[i]);
                    *eq = '=';
                } else {
                    if (!is_valid_name_zstring(argv[i])) {
                        fprintf(stderr, "export: %s: not a valid identifier\n", argv[i]);
                        status = 1;
                        continue;
                    }
                    variable_store_export_variable(exec->vars, argv[i]);
                }
            }
            executor_set_status(exec, status);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_SUCCESS;
        }
        // readonly
        if (strcmp(argv[0], "readonly") == 0) {
            if (argc == 1) {
                variable_store_dump_variables(exec->vars);
                executor_set_status(exec, 0);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_SUCCESS;
            }
            int status = 0;
            for (int i = 1; i < argc; i++) {
                char *eq = strchr(argv[i], '=');
                if (eq) {
                    *eq = '\0';
                    if (!is_valid_name_zstring(argv[i])) {
                        fprintf(stderr, "readonly: %s: not a valid identifier\n", argv[i]);
                        status = 1;
                        continue;
                    }
                    variable_store_set_variable(exec->vars, argv[i], eq + 1);
                    variable_store_make_readonly(exec->vars, argv[i]);
                    *eq = '=';
                } else {
                    if (!is_valid_name_zstring(argv[i])) {
                        fprintf(stderr, "readonly: %s: not a valid identifier\n", argv[i]);
                        status = 1;
                        continue;
                    }
                    variable_store_make_readonly(exec->vars, argv[i]);
                }
            }
            executor_set_status(exec, status);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_SUCCESS;
        }
        // set
        if (strcmp(argv[0], "set") == 0) {
            if (argc == 1) {
                variable_store_dump_variables(exec->vars);
                executor_set_status(exec, 0);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_SUCCESS;
            }
            ptr_array_clear(exec->vars->positional_params);
            for (int i = 1; i < argc; i++) {
                ptr_array_append(exec->vars->positional_params, strdup(argv[i]));
            }
            executor_set_status(exec, 0);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_SUCCESS;
        }
        // shift
        if (strcmp(argv[0], "shift") == 0) {
            int n = 1;
            if (argc > 1) {
                char *endptr;
                long val = strtol(argv[1], &endptr, 10);
                if (*endptr != '\0' || val < 0) {
                    fprintf(stderr, "shift: %s: invalid number\n", argv[1]);
                    executor_set_status(exec, 2);
                    for (int i = 0; i < argc; i++) free(argv[i]);
                    free(argv);
                    return EXEC_FAILURE;
                }
                n = (int)val;
            }
            if (argc > 2) {
                fprintf(stderr, "shift: too many arguments\n");
                executor_set_status(exec, 2);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            if (n > exec->vars->positional_params->len) {
                fprintf(stderr, "shift: shift count out of range\n");
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            for (int i = 0; i < n; i++) {
                if (exec->vars->positional_params->len > 0) {
                    free(exec->vars->positional_params->data[0]);
                    ptr_array_remove(exec->vars->positional_params, 0);
                }
            }
            executor_set_status(exec, 0);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_SUCCESS;
        }
        // times
        if (strcmp(argv[0], "times") == 0) {
            if (argc > 1) {
                fprintf(stderr, "times: too many arguments\n");
                executor_set_status(exec, 2);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            struct tms tms;
            clock_t real = times(&tms);
            if (real == (clock_t)-1) {
                fprintf(stderr, "times: %s\n", strerror(errno));
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            clock_t ticks_per_sec = sysconf(_SC_CLK_TCK);
            printf("%ldm%lds %ldm%lds\n",
                   tms.tms_utime / ticks_per_sec / 60, tms.tms_utime / ticks_per_sec % 60,
                   tms.tms_stime / ticks_per_sec / 60, tms.tms_stime / ticks_per_sec % 60);
            printf("%ldm%lds %ldm%lds\n",
                   tms.tms_cutime / ticks_per_sec / 60, tms.tms_cutime / ticks_per_sec % 60,
                   tms.tms_cstime / ticks_per_sec / 60, tms.tms_cstime / ticks_per_sec % 60);
            executor_set_status(exec, 0);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_SUCCESS;
        }
        // trap
        if (strcmp(argv[0], "trap") == 0) {
            if (argc == 1) {
                // List traps
                for (int i = 0; i < exec->trap_store->traps->len; i++) {
                    Trap *trap = exec->trap_store->traps->data[i];
                    const char *sig_name = NULL;
                    switch (trap->signal) {
                        case SIGHUP: sig_name = "HUP"; break;
                        case SIGINT: sig_name = "INT"; break;
                        case SIGQUIT: sig_name = "QUIT"; break;
                        case SIGTERM: sig_name = "TERM"; break;
                        case SIGKILL: sig_name = "KILL"; break;
                        case SIGUSR1: sig_name = "USR1"; break;
                        case SIGUSR2: sig_name = "USR2"; break;
                        default: continue;
                    }
                    if (trap->action) {
                        printf("trap %s %s\n", string_cstr(trap->action), sig_name);
                    } else {
                        printf("trap '' %s\n", sig_name);
                    }
                }
                executor_set_status(exec, 0);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_SUCCESS;
            }
            if (argc == 2) {
                fprintf(stderr, "trap: trap requires arguments\n");
                executor_set_status(exec, 2);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            char *action = argv[1];
            int status = 0;
            for (int i = 2; i < argc; i++) {
                int sig = get_signal_number(argv[i]);
                if (sig == -1) {
                    fprintf(stderr, "trap: %s: invalid signal specification\n", argv[i]);
                    status = 1;
                    continue;
                }
                trap_store_set(exec->trap_store, sig, action);
                struct sigaction sa;
                memset(&sa, 0, sizeof(sa));
                if (*action == '\0') {
                    sa.sa_handler = SIG_DFL;
                } else if (strcmp(action, "-") == 0) {
                    sa.sa_handler = SIG_IGN;
                } else {
                    sa.sa_handler = signal_handler;
                }
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;
                if (sigaction(sig, &sa, NULL) == -1) {
                    fprintf(stderr, "trap: sigaction failed for %s: %s\n", argv[i], strerror(errno));
                    status = 1;
                }
            }
            executor_set_status(exec, status);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_SUCCESS;
        }
        // unset
        if (strcmp(argv[0], "unset") == 0) {
            int unset_vars = 1, unset_funcs = 0;
            int optind = 1;
            if (argc > 1 && argv[1][0] == '-') {
                if (strcmp(argv[1], "-v") == 0) {
                    unset_vars = 1;
                    unset_funcs = 0;
                    optind = 2;
                } else if (strcmp(argv[1], "-f") == 0) {
                    unset_vars = 0;
                    unset_funcs = 1;
                    optind = 2;
                } else {
                    fprintf(stderr, "unset: %s: invalid option\n", argv[1]);
                    executor_set_status(exec, 2);
                    for (int i = 0; i < argc; i++) free(argv[i]);
                    free(argv);
                    return EXEC_FAILURE;
                }
            }
            if (optind >= argc) {
                executor_set_status(exec, 0);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_SUCCESS;
            }
            int status = 0;
            for (int i = optind; i < argc; i++) {
                if (unset_vars && !is_valid_name_zstring(argv[i])) {
                    fprintf(stderr, "unset: %s: not a valid identifier\n", argv[i]);
                    status = 1;
                    continue;
                }
                if (unset_vars) {
                    variable_store_unset_variable(exec->vars, argv[i]);
                }
                if (unset_funcs) {
                    for (int j = 0; j < exec->func_store->functions->len; j++) {
                        Function *func = exec->func_store->functions->data[j];
                        if (strcmp(func->name, argv[i]) == 0) {
                            free(func->name);
                            free(func);
                            ptr_array_remove(exec->func_store->functions, j);
                            break;
                        }
                    }
                }
            }
            executor_set_status(exec, status);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_SUCCESS;
        }
        // return
        if (strcmp(argv[0], "return") == 0) {
            if (exec->function_depth == 0) {
                fprintf(stderr, "return: can only be used in a function\n");
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            int status = 0;
            if (argc > 1) {
                char *endptr;
                long n = strtol(argv[1], &endptr, 10);
                if (*endptr != '\0') {
                    fprintf(stderr, "return: %s: numeric argument required\n", argv[1]);
                    executor_set_status(exec, 1);
                    for (int i = 0; i < argc; i++) free(argv[i]);
                    free(argv);
                    return EXEC_FAILURE;
                }
                status = (int)n;
            }
            if (argc > 2) {
                fprintf(stderr, "return: too many arguments\n");
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            executor_set_status(exec, status);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_RETURN;
        }
        // break
        if (strcmp(argv[0], "break") == 0) {
            if (exec->loop_depth == 0) {
                fprintf(stderr, "break: only meaningful in a loop\n");
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            int count = 1;
            if (argc > 1) {
                char *endptr;
                long n = strtol(argv[1], &endptr, 10);
                if (*endptr != '\0' || n <= 0) {
                    fprintf(stderr, "break: %s: numeric argument required\n", argv[1]);
                    executor_set_status(exec, 1);
                    for (int i = 0; i < argc; i++) free(argv[i]);
                    free(argv);
                    return EXEC_FAILURE;
                }
                count = (int)n;
                if (count > exec->loop_depth) {
                    fprintf(stderr, "break: %d: loop count out of range\n", count);
                    executor_set_status(exec, 1);
                    for (int i = 0; i < argc; i++) free(argv[i]);
                    free(argv);
                    return EXEC_FAILURE;
                }
            }
            if (argc > 2) {
                fprintf(stderr, "break: too many arguments\n");
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            exec->break_count = count;
            executor_set_status(exec, 0);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_BREAK;
        }
        // continue
        if (strcmp(argv[0], "continue") == 0) {
            if (exec->loop_depth == 0) {
                fprintf(stderr, "continue: only meaningful in a loop\n");
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            int count = 1;
            if (argc > 1) {
                char *endptr;
                long n = strtol(argv[1], &endptr, 10);
                if (*endptr != '\0' || n <= 0) {
                    fprintf(stderr, "continue: %s: numeric argument required\n", argv[1]);
                    executor_set_status(exec, 1);
                    for (int i = 0; i < argc; i++) free(argv[i]);
                    free(argv);
                    return EXEC_FAILURE;
                }
                count = (int)n;
                if (count > exec->loop_depth) {
                    fprintf(stderr, "continue: %d: loop count out of range\n", count);
                    executor_set_status(exec, 1);
                    for (int i = 0; i < argc; i++) free(argv[i]);
                    free(argv);
                    return EXEC_FAILURE;
                }
            }
            if (argc > 2) {
                fprintf(stderr, "continue: too many arguments\n");
                executor_set_status(exec, 1);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return EXEC_FAILURE;
            }
            exec->continue_count = count;
            executor_set_status(exec, 0);
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            return EXEC_CONTINUE;
        }
        // Function call
        Function *func = function_store_get(exec->func_store, argv[0]);
        if (func) {
            char **old_params = (char **)exec->vars->positional_params->data;
            int old_len = exec->vars->positional_params->len;
            ptr_array_clear(exec->vars->positional_params);
            for (int i = 1; i < argc; i++) {
                ptr_array_append(exec->vars->positional_params, strdup(argv[i]));
            }
            exec->function_depth++;
            ExecStatus ret = executor_run(exec, func->body);
            exec->function_depth--;
            ptr_array_clear(exec->vars->positional_params);
            for (int i = 0; i < old_len; i++) {
                ptr_array_append(exec->vars->positional_params, old_params[i]);
            }
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            if (ret == EXEC_RETURN) {
                return EXEC_SUCCESS;
            }
            return ret;
        }
    }

    // Execute external command
    if (!argv || !argv[0]) {
        executor_set_status(exec, 0);
        free(argv);
        return EXEC_SUCCESS;
    }

    pid_t pid = fork();
    if (pid == -1) {
        executor_set_status(exec, 1);
        for (int i = 0; i < argc; i++) free(argv[i]);
        free(argv);
        return EXEC_FAILURE;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        perror(argv[0]);
        exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    executor_set_status(exec, WIFEXITED(status) ? WEXITSTATUS(status) : 1);

    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    return EXEC_SUCCESS;
}

// Execute a pipeline
static ExecStatus exec_pipeline(Executor *exec, ASTNode *node) {
    int pipefd[2];
    pid_t *pids = calloc(node->data.pipeline.command_count, sizeof(pid_t));
    int status = 0;

    for (int i = 0; i < node->data.pipeline.command_count; i++) {
        if (i < node->data.pipeline.command_count - 1) {
            if (pipe(pipefd) == -1) {
                free(pids);
                executor_set_status(exec, 1);
                return EXEC_FAILURE;
            }
        }

        pid_t pid = fork();
        if (pid == -1) {
            free(pids);
            executor_set_status(exec, 1);
            return EXEC_FAILURE;
        }

        if (pid == 0) {
            if (i > 0) {
                dup2(exec->pipe_fds[0], STDIN_FILENO);
                close(exec->pipe_fds[0]);
            }
            if (i < node->data.pipeline.command_count - 1) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }
            ExecStatus ret = executor_run(exec, node->data.pipeline.commands[i]);
            exit(ret == EXEC_SUCCESS ? 0 : 1);
        }

        pids[i] = pid;
        if (i > 0) {
            close(exec->pipe_fds[0]);
        }
        if (i < node->data.pipeline.command_count - 1) {
            exec->pipe_fds[0] = pipefd[0];
            close(pipefd[1]);
        }
    }

    for (int i = 0; i < node->data.pipeline.command_count; i++) {
        int child_status;
        waitpid(pids[i], &child_status, 0);
        if (i == node->data.pipeline.command_count - 1) {
            status = WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 1;
        }
    }

    free(pids);
    executor_set_status(exec, node->data.pipeline.bang ? !status : status);
    return EXEC_SUCCESS;
}

// Pattern matching for case
static int match_pattern(const char *word, const char *pattern) {
    if (!word || !pattern) return 0;

    while (*word && *pattern) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 1;
            while (*word) {
                if (match_pattern(word, pattern)) return 1;
                word++;
            }
            return 0;
        } else if (*pattern == '?' || *word == *pattern) {
            word++;
            pattern++;
        } else {
            return 0;
        }
    }

    return (*word == 0 && *pattern == 0) || (*pattern == '*' && *(pattern + 1) == 0);
}

// Signal number lookup
static int get_signal_number(const char *sig) {
    if (strcmp(sig, "HUP") == 0) return SIGHUP;
    if (strcmp(sig, "INT") == 0) return SIGINT;
    if (strcmp(sig, "QUIT") == 0) return SIGQUIT;
    if (strcmp(sig, "TERM") == 0) return SIGTERM;
    if (strcmp(sig, "KILL") == 0) return SIGKILL;
    if (strcmp(sig, "USR1") == 0) return SIGUSR1;
    if (strcmp(sig, "USR2") == 0) return SIGUSR2;
    char *endptr;
    long n = strtol(sig, &endptr, 10);
    if (*endptr == '\0' && n >= 0 && n <= 31) return (int)n;
    return -1;
}

// Execute AST node
ExecStatus executor_run(Executor *exec, ASTNode *ast) {
    if (!ast) {
        executor_set_status(exec, 0);
        return EXEC_SUCCESS;
    }

    switch (ast->type) {
        case AST_SIMPLE_COMMAND:
            return exec_simple_command(exec, ast);
        case AST_PIPELINE:
            return exec_pipeline(exec, ast);
        case AST_AND_OR: {
            ExecStatus left = executor_run(exec, ast->data.and_or.left);
            if (left == EXEC_RETURN) return left;
            if (left != EXEC_SUCCESS) return left;
            int status = executor_get_status(exec);
            if ((ast->data.and_or.operation == TOKEN_AND_IF && status == 0) ||
                (ast->data.and_or.operation == TOKEN_OR_IF && status != 0)) {
                return executor_run(exec, ast->data.and_or.right);
            }
            return EXEC_SUCCESS;
        }
        case AST_LIST: {
            ExecStatus ret = executor_run(exec, ast->data.list.and_or);
            if (ret == EXEC_RETURN) return ret;
            if (ret != EXEC_SUCCESS) return ret;
            if (ast->data.list.next && ast->data.list.separator != TOKEN_AMP) {
                return executor_run(exec, ast->data.list.next);
            }
            return EXEC_SUCCESS;
        }
        case AST_COMPLETE_COMMAND:
            return executor_run(exec, ast->data.complete_command.list);
        case AST_PROGRAM:
            return executor_run(exec, ast->data.program.commands);
        case AST_IF_CLAUSE: {
            ExecStatus cond_status = executor_run(exec, ast->data.if_clause.condition);
            if (cond_status == EXEC_RETURN) return cond_status;
            if (cond_status != EXEC_SUCCESS) return cond_status;

            if (executor_get_status(exec) == 0) {
                ExecStatus then_status = executor_run(exec, ast->data.if_clause.then_body);
                if (then_status == EXEC_RETURN) return then_status;
                if (then_status != EXEC_SUCCESS) return then_status;
            } else if (ast->data.if_clause.else_part) {
                ExecStatus else_status = executor_run(exec, ast->data.if_clause.else_part);
                if (else_status == EXEC_RETURN) return else_status;
                if (else_status != EXEC_SUCCESS) return else_status;
            }

            if (ast->data.if_clause.next) {
                return executor_run(exec, ast->data.if_clause.next);
            }
            return EXEC_SUCCESS;
        }
        case AST_FOR_CLAUSE: {
            exec->loop_depth++;
            char **words = ast->data.for_clause.wordlist;
            int count = ast->data.for_clause.wordlist_count;
            if (count == 0) {
                words = (char **)exec->vars->positional_params->data;
                count = exec->vars->positional_params->len;
            }

            for (int i = 0; i < count; i++) {
                if (exec->break_count > 0) {
                    exec->break_count--;
                    break;
                }
                if (exec->continue_count > 0) {
                    exec->continue_count--;
                    continue;
                }

                variable_store_set_variable(exec->vars, ast->data.for_clause.variable, words[i]);

                ExecStatus body_status = executor_run(exec, ast->data.for_clause.body);
                if (body_status == EXEC_RETURN) {
                    exec->loop_depth--;
                    return body_status;
                }
                if (body_status == EXEC_BREAK) {
                    exec->break_count--;
                    break;
                }
                if (body_status == EXEC_CONTINUE) {
                    exec->continue_count--;
                    continue;
                }
                if (body_status != EXEC_SUCCESS) {
                    exec->loop_depth--;
                    return body_status;
                }
            }

            exec->loop_depth--;

            if (ast->data.for_clause.next) {
                return executor_run(exec, ast->data.for_clause.next);
            }
            return EXEC_SUCCESS;
        }
        case AST_WHILE_CLAUSE: {
            exec->loop_depth++;
            while (1) {
                if (exec->break_count > 0) {
                    exec->break_count--;
                    break;
                }
                if (exec->continue_count > 0) {
                    exec->continue_count--;
                    continue;
                }

                ExecStatus cond_status = executor_run(exec, ast->data.while_clause.condition);
                if (cond_status == EXEC_RETURN) {
                    exec->loop_depth--;
                    return cond_status;
                }
                if (cond_status != EXEC_SUCCESS) {
                    exec->loop_depth--;
                    return cond_status;
                }

                if (executor_get_status(exec) != 0) break;

                ExecStatus body_status = executor_run(exec, ast->data.while_clause.body);
                if (body_status == EXEC_RETURN) {
                    exec->loop_depth--;
                    return body_status;
                }
                if (body_status == EXEC_BREAK) {
                    exec->break_count--;
                    break;
                }
                if (body_status == EXEC_CONTINUE) {
                    exec->continue_count--;
                    continue;
                }
                if (body_status != EXEC_SUCCESS) {
                    exec->loop_depth--;
                    return body_status;
                }
            }

            exec->loop_depth--;

            if (ast->data.while_clause.next) {
                return executor_run(exec, ast->data.while_clause.next);
            }
            return EXEC_SUCCESS;
        }
        case AST_UNTIL_CLAUSE: {
            exec->loop_depth++;
            while (1) {
                if (exec->break_count > 0) {
                    exec->break_count--;
                    break;
                }
                if (exec->continue_count > 0) {
                    exec->continue_count--;
                    continue;
                }

                ExecStatus cond_status = executor_run(exec, ast->data.until_clause.condition);
                if (cond_status == EXEC_RETURN) {
                    exec->loop_depth--;
                    return cond_status;
                }
                if (cond_status != EXEC_SUCCESS) {
                    exec->loop_depth--;
                    return cond_status;
                }

                if (executor_get_status(exec) == 0) break;

                ExecStatus body_status = executor_run(exec, ast->data.until_clause.body);
                if (body_status == EXEC_RETURN) {
                    exec->loop_depth--;
                    return body_status;
                }
                if (body_status == EXEC_BREAK) {
                    exec->break_count--;
                    break;
                }
                if (body_status == EXEC_CONTINUE) {
                    exec->continue_count--;
                    continue;
                }
                if (body_status != EXEC_SUCCESS) {
                    exec->loop_depth--;
                    return body_status;
                }
            }

            exec->loop_depth--;

            if (ast->data.until_clause.next) {
                return executor_run(exec, ast->data.until_clause.next);
            }
            return EXEC_SUCCESS;
        }
        case AST_CASE_CLAUSE: {
            char *word = ast->data.case_clause.word;

            CaseItem *item = ast->data.case_clause.items;
            int matched = 0;
            while (item && !matched) {
                for (int i = 0; i < item->pattern_count; i++) {
                    if (match_pattern(word, item->patterns[i])) {
                        ExecStatus action_status = executor_run(exec, item->action);
                        if (action_status == EXEC_RETURN) return action_status;
                        if (action_status != EXEC_SUCCESS) return action_status;
                        matched = 1;
                        break;
                    }
                }
                if (item->has_dsemi) break;
                item = item->next;
            }

            if (ast->data.case_clause.next) {
                return executor_run(exec, ast->data.case_clause.next);
            }
            return EXEC_SUCCESS;
        }
        case AST_BRACE_GROUP: {
            ExecStatus ret = executor_run(exec, ast->data.brace_group.body);
            if (ret == EXEC_RETURN) return ret;
            return ret;
        }
        case AST_SUBSHELL: {
            pid_t pid = fork();
            if (pid == -1) {
                executor_set_status(exec, 1);
                return EXEC_FAILURE;
            }
            if (pid == 0) {
                exec->in_subshell = 1;
                ExecStatus ret = executor_run(exec, ast->data.subshell.body);
                exit(ret == EXEC_SUCCESS || ret == EXEC_RETURN ? executor_get_status(exec) : 1);
            }
            int status;
            waitpid(pid, &status, 0);
            executor_set_status(exec, WIFEXITED(status) ? WEXITSTATUS(status) : 1);
            return EXEC_SUCCESS;
        }
        case AST_FUNCTION_DEFINITION: {
            int ret = function_store_set(exec->func_store, ast->data.function_definition.name, ast->data.function_definition.body);
            if (ret != 0) {
                executor_set_status(exec, 1);
            } else {
                executor_set_status(exec, 0);
            }
            if (ast->data.function_definition.next) {
                return executor_run(exec, ast->data.function_definition.next);
            }
            return EXEC_SUCCESS;
        }
        case AST_IO_REDIRECT:
            return EXEC_SUCCESS;
        case AST_EXPANSION:
            return EXEC_SUCCESS;
        default:
            executor_set_status(exec, 1);
            return EXEC_FAILURE;
    }
}
