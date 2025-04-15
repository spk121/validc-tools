#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/times.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "builtins.h"
#include "string.h"
#include "tokenizer.h"
#include "parser.h"

bool is_special_builtin(const char *name) {
    const char *special_builtins[] = {
        ":", ".", "eval", "exec", "exit", "export", "readonly",
        "set", "shift", "times", "trap", "unset", "break", "continue", "return",
        NULL
    };
    for (int i = 0; special_builtins[i]; i++) {
        if (strcmp(name, special_builtins[i]) == 0) {
            return true;
        }
    }
    return false;
}

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

static ExecStatus builtin_colon(Executor *exec, char **argv, int argc) {
    executor_set_status(exec, 0);
    return EXEC_SUCCESS;
}

static ExecStatus builtin_dot(Executor *exec, char **argv, int argc) {
    if (argc < 2) {
        fprintf(stderr, ".: filename argument required\n");
        executor_set_status(exec, 2);
        if (!exec->is_interactive) exit(2); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, ".: %s: %s\n", argv[1], strerror(errno));
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
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
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
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
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    ExecStatus ret = executor_run(exec, ast);
    parser_destroy(parser);
    string_destroy(script);
    return ret;
}

static ExecStatus builtin_eval(Executor *exec, char **argv, int argc) {
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
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
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
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    ExecStatus ret = executor_run(exec, ast);
    parser_destroy(parser);
    string_destroy(input);
    return ret;
}

static ExecStatus builtin_exec(Executor *exec, char **argv, int argc) {
    if (argc == 1) {
        executor_set_status(exec, 0);
        return EXEC_SUCCESS;
    }
    execvp(argv[1], &argv[1]);
    fprintf(stderr, "exec: %s: %s\n", argv[1], strerror(errno));
    executor_set_status(exec, 1);
    if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
    return EXEC_FAILURE;
}

static ExecStatus builtin_exit(Executor *exec, char **argv, int argc) {
    int status = exec->last_status;
    if (argc > 1) {
        char *endptr;
        long n = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0') {
            fprintf(stderr, "exit: %s: numeric argument required\n", argv[1]);
            status = 2;
            if (!exec->is_interactive) exit(2); // Exit in non-interactive mode
        } else {
            status = (int)n;
        }
    }
    if (argc > 2) {
        fprintf(stderr, "exit: too many arguments\n");
        status = 2;
        if (!exec->is_interactive) exit(2); // Exit in non-interactive mode
    }
    exit(status);
    return EXEC_SUCCESS; // Unreachable
}

static ExecStatus builtin_export(Executor *exec, char **argv, int argc) {
    if (argc == 1) {
        variable_store_dump_variables(exec->vars);
        executor_set_status(exec, 0);
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
                *eq = '=';
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
    if (status != 0 && !exec->is_interactive) exit(status); // Exit in non-interactive mode
    return EXEC_SUCCESS;
}

static ExecStatus builtin_readonly(Executor *exec, char **argv, int argc) {
    if (argc == 1) {
        variable_store_dump_variables(exec->vars);
        executor_set_status(exec, 0);
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
                *eq = '=';
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
    if (status != 0 && !exec->is_interactive) exit(status); // Exit in non-interactive mode
    return EXEC_SUCCESS;
}

static ExecStatus builtin_set(Executor *exec, char **argv, int argc) {
    if (argc == 1) {
        variable_store_dump_variables(exec->vars);
        executor_set_status(exec, 0);
        return EXEC_SUCCESS;
    }
    ptr_array_clear(exec->vars->positional_params);
    for (int i = 1; i < argc; i++) {
        ptr_array_append(exec->vars->positional_params, strdup(argv[i]));
    }
    executor_set_status(exec, 0);
    return EXEC_SUCCESS;
}

static ExecStatus builtin_shift(Executor *exec, char **argv, int argc) {
    int n = 1;
    if (argc > 1) {
        char *endptr;
        long val = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0' || val < 0) {
            fprintf(stderr, "shift: %s: invalid number\n", argv[1]);
            executor_set_status(exec, 2);
            if (!exec->is_interactive) exit(2); // Exit in non-interactive mode
            return EXEC_FAILURE;
        }
        n = (int)val;
    }
    if (argc > 2) {
        fprintf(stderr, "shift: too many arguments\n");
        executor_set_status(exec, 2);
        if (!exec->is_interactive) exit(2); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    if (n > exec->vars->positional_params->len) {
        fprintf(stderr, "shift: shift count out of range\n");
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    for (int i = 0; i < n; i++) {
        if (exec->vars->positional_params->len > 0) {
            free(exec->vars->positional_params->data[0]);
            ptr_array_remove(exec->vars->positional_params, 0);
        }
    }
    executor_set_status(exec, 0);
    return EXEC_SUCCESS;
}

static ExecStatus builtin_times(Executor *exec, char **argv, int argc) {
    if (argc > 1) {
        fprintf(stderr, "times: too many arguments\n");
        executor_set_status(exec, 2);
        if (!exec->is_interactive) exit(2); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    struct tms tms;
    clock_t real = times(&tms);
    if (real == (clock_t)-1) {
        fprintf(stderr, "times: %s\n", strerror(errno));
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
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
    return EXEC_SUCCESS;
}

static ExecStatus builtin_trap(Executor *exec, char **argv, int argc) {
    if (argc == 1) {
        trap_store_print(exec->trap_store);
        executor_set_status(exec, 0);
        return EXEC_SUCCESS;
    }
    if (argc == 2) {
        fprintf(stderr, "trap: trap requires arguments\n");
        executor_set_status(exec, 2);
        if (!exec->is_interactive) exit(2); // Exit in non-interactive mode
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
    if (status != 0 && !exec->is_interactive) exit(status); // Exit in non-interactive mode
    return EXEC_SUCCESS;
}

static ExecStatus builtin_unset(Executor *exec, char **argv, int argc) {
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
            if (!exec->is_interactive) exit(2); // Exit in non-interactive mode
            return EXEC_FAILURE;
        }
    }
    if (optind >= argc) {
        executor_set_status(exec, 0);
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
    if (status != 0 && !exec->is_interactive) exit(status); // Exit in non-interactive mode
    return EXEC_SUCCESS;
}

static ExecStatus builtin_return(Executor *exec, char **argv, int argc) {
    if (exec->function_depth == 0) {
        fprintf(stderr, "return: can only be used in a function\n");
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    int status = 0;
    if (argc > 1) {
        char *endptr;
        long n = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0') {
            fprintf(stderr, "return: %s: numeric argument required\n", argv[1]);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
            return EXEC_FAILURE;
        }
        status = (int)n;
    }
    if (argc > 2) {
        fprintf(stderr, "return: too many arguments\n");
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    executor_set_status(exec, status);
    return EXEC_RETURN;
}

static ExecStatus builtin_break(Executor *exec, char **argv, int argc) {
    if (exec->loop_depth == 0) {
        fprintf(stderr, "break: only meaningful in a loop\n");
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    int count = 1;
    if (argc > 1) {
        char *endptr;
        long n = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0' || n <= 0) {
            fprintf(stderr, "break: %s: numeric argument required\n", argv[1]);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
            return EXEC_FAILURE;
        }
        count = (int)n;
        if (count > exec->loop_depth) {
            fprintf(stderr, "break: %d: loop count out of range\n", count);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
            return EXEC_FAILURE;
        }
    }
    if (argc > 2) {
        fprintf(stderr, "break: too many arguments\n");
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    exec->break_count = count;
    executor_set_status(exec, 0);
    return EXEC_BREAK;
}

static ExecStatus builtin_continue(Executor *exec, char **argv, int argc) {
    if (exec->loop_depth == 0) {
        fprintf(stderr, "continue: only meaningful in a loop\n");
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    int count = 1;
    if (argc > 1) {
        char *endptr;
        long n = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0' || n <= 0) {
            fprintf(stderr, "continue: %s: numeric argument required\n", argv[1]);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
            return EXEC_FAILURE;
        }
        count = (int)n;
        if (count > exec->loop_depth) {
            fprintf(stderr, "continue: %d: loop count out of range\n", count);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
            return EXEC_FAILURE;
        }
    }
    if (argc > 2) {
        fprintf(stderr, "continue: too many arguments\n");
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1); // Exit in non-interactive mode
        return EXEC_FAILURE;
    }
    exec->continue_count = count;
    executor_set_status(exec, 0);
    return EXEC_CONTINUE;
}

ExecStatus builtin_execute(Executor *exec, char **argv, int argc) {
    if (!argv || !argv[0]) {
        executor_set_status(exec, 0);
        return EXEC_SUCCESS;
    }

    struct {
        const char *name;
        ExecStatus (*func)(Executor *, char **, int);
    } builtins[] = {
        { ":", builtin_colon },
        { ".", builtin_dot },
        { "eval", builtin_eval },
        { "exec", builtin_exec },
        { "exit", builtin_exit },
        { "export", builtin_export },
        { "readonly", builtin_readonly },
        { "set", builtin_set },
        { "shift", builtin_shift },
        { "times", builtin_times },
        { "trap", builtin_trap },
        { "unset", builtin_unset },
        { "return", builtin_return },
        { "break", builtin_break },
        { "continue", builtin_continue },
        { NULL, NULL }
    };

    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(argv[0], builtins[i].name) == 0) {
            ExecStatus ret = builtins[i].func(exec, argv, argc);
            for (int j = 0; j < argc; j++) free(argv[j]);
            free(argv);
            return ret;
        }
    }

    // Not a builtin
    for (int j = 0; j < argc; j++) free(argv[j]);
    free(argv);
    return EXEC_NOT_BUILTIN;
}
