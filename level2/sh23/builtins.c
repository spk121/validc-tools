#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/times.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <sys/stat.h>
#include "builtins.h"
#include "string.h"
#include "tokenizer.h"
#include "parser.h"

bool is_special_builtin(const char *name) {
    const char *special_builtins[] = {
        ":", ".", "eval", "exec", "exit", "export", "readonly",
        "set", "shift", "times", "trap", "unset", "break", "continue", "return",
        "cd", NULL
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

static ExecStatus builtin_cd(Executor *exec, char **argv, int argc) {
    bool physical = false; // -P: resolve symlinks
    const char *dir = NULL;
    int optind = 1;

    // Parse options
    if (argc > 1 && argv[1][0] == '-') {
        if (strcmp(argv[1], "-L") == 0) {
            physical = false;
            optind = 2;
        } else if (strcmp(argv[1], "-P") == 0) {
            physical = true;
            optind = 2;
        } else if (strcmp(argv[1], "--") == 0) {
            optind = 2;
        } else {
            fprintf(stderr, "cd: %s: invalid option\n", argv[1]);
            executor_set_status(exec, 2);
            if (!exec->is_interactive) exit(2);
            return EXEC_FAILURE;
        }
    }

    // Determine directory
    if (optind < argc) {
        dir = argv[optind];
        if (optind + 1 < argc) {
            fprintf(stderr, "cd: too many arguments\n");
            executor_set_status(exec, 2);
            if (!exec->is_interactive) exit(2);
            return EXEC_FAILURE;
        }
    }

    // Handle special cases: no dir (use $HOME), or dir is "-"
    if (!dir || dir[0] == '\0') {
        dir = variable_store_get_variable(exec->vars, "HOME");
        if (!dir || dir[0] == '\0') {
            fprintf(stderr, "cd: HOME not set\n");
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1);
            return EXEC_FAILURE;
        }
    } else if (strcmp(dir, "-") == 0) {
        dir = variable_store_get_variable(exec->vars, "OLDPWD");
        if (!dir || dir[0] == '\0') {
            fprintf(stderr, "cd: OLDPWD not set\n");
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1);
            return EXEC_FAILURE;
        }
        // Print directory when using "-"
        printf("%s\n", dir);
    }

    // Save current PWD for OLDPWD
    char *oldpwd = getcwd(NULL, 0);
    if (!oldpwd) {
        fprintf(stderr, "cd: getcwd: %s\n", strerror(errno));
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1);
        return EXEC_FAILURE;
    }

    // Change directory
    if (chdir(dir) != 0) {
        fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
        free(oldpwd);
        executor_set_status(exec, 1);
        if (!exec->is_interactive) exit(1);
        return EXEC_FAILURE;
    }

    // Get new PWD
    char *newpwd = NULL;
    if (physical) {
        // -P: Use physical path
        newpwd = getcwd(NULL, 0);
        if (!newpwd) {
            fprintf(stderr, "cd: getcwd: %s\n", strerror(errno));
            free(oldpwd);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1);
            return EXEC_FAILURE;
        }
    } else {
        // -L: Use logical path (dir, possibly resolved relative to current dir)
        if (dir[0] == '/' || !variable_store_get_variable(exec->vars, "PWD")) {
            // Absolute path or no PWD: use dir as-is
            newpwd = realpath(dir, NULL);
            if (!newpwd) {
                fprintf(stderr, "cd: realpath: %s\n", strerror(errno));
                free(oldpwd);
                executor_set_status(exec, 1);
                if (!exec->is_interactive) exit(1);
                return EXEC_FAILURE;
            }
        } else {
            // Relative path: Construct logical path
            String *path = string_create();
            string_append_zstring(path, variable_store_get_variable(exec->vars, "PWD"));
            if (path->buffer[path->len - 1] != '/') {
                string_append_char(path, '/');
            }
            string_append_zstring(path, dir);
            newpwd = realpath(string_cstr(path), NULL);
            string_destroy(path);
            if (!newpwd) {
                fprintf(stderr, "cd: realpath: %s\n", strerror(errno));
                free(oldpwd);
                executor_set_status(exec, 1);
                if (!exec->is_interactive) exit(1);
                return EXEC_FAILURE;
            }
        }
    }

    // Update environment variables
    variable_store_set_variable(exec->vars, "OLDPWD", oldpwd);
    variable_store_set_variable(exec->vars, "PWD", newpwd);
    free(oldpwd);
    free(newpwd);

    executor_set_status(exec, 0);
    return EXEC_SUCCESS;
}


static ExecStatus builtin_test(Executor *exec, char **argv, int argc) {
    bool is_bracket = (strcmp(argv[0], "[") == 0);
    int arg_count = argc - 1; // Exclude argv[0]
    char **args = argv + 1;   // Skip command name

    // For [, require ] as last argument
    if (is_bracket) {
        if (arg_count == 0 || strcmp(args[arg_count - 1], "]") != 0) {
            fprintf(stderr, "%s: missing ]\n", argv[0]);
            executor_set_status(exec, 2);
            return EXEC_FAILURE;
        }
        arg_count--; // Exclude ]
        args[arg_count] = NULL;
    }

    // Forward declaration for recursive parsing
    static bool evaluate_expr(Executor *exec, char **args, int *pos, int max_args);

    // Evaluate expression
    int pos = 0;
    bool result = evaluate_expr(exec, args, &pos, arg_count);

    // Check if all arguments consumed
    if (pos != arg_count && !result) {
        fprintf(stderr, "%s: too many arguments\n", argv[0]);
        executor_set_status(exec, 2);
        return EXEC_FAILURE;
    }

    executor_set_status(exec, result ? 0 : 1);
    return EXEC_SUCCESS;
}

// Recursive expression evaluation
static bool evaluate_expr(Executor *exec, char **args, int *pos, int max_args) {
    if (*pos >= max_args) {
        return false; // Empty expression is false
    }

    // Handle !
    if (strcmp(args[*pos], "!") == 0) {
        (*pos)++;
        if (*pos >= max_args) {
            fprintf(stderr, "%s: unary operator expected\n", args[0]);
            executor_set_status(exec, 2);
            return false;
        }
        bool result = evaluate_expr(exec, args, pos, max_args);
        return !result;
    }

    // Handle ( )
    if (strcmp(args[*pos], "(") == 0) {
        (*pos)++;
        if (*pos >= max_args) {
            fprintf(stderr, "%s: missing )\n", args[0]);
            executor_set_status(exec, 2);
            return false;
        }
        bool result = evaluate_expr(exec, args, pos, max_args);
        if (*pos >= max_args || strcmp(args[*pos], ")") != 0) {
            fprintf(stderr, "%s: missing )\n", args[0]);
            executor_set_status(exec, 2);
            return false;
        }
        (*pos)++;
        return result;
    }

    // Primary expression
    bool left_result;
    if (*pos + 1 < max_args && strcmp(args[*pos + 1], "-o") != 0 && strcmp(args[*pos + 1], "-a") != 0) {
        // Try binary or unary operator
        if (*pos + 2 < max_args) {
            // Binary operators: =, !=, -eq, -ne, etc.
            char *arg1 = args[*pos];
            char *op = args[*pos + 1];
            char *arg2 = args[*pos + 2];

            if (strcmp(op, "=") == 0) {
                left_result = strcmp(arg1, arg2) == 0;
                *pos += 3;
            } else if (strcmp(op, "!=") == 0) {
                left_result = strcmp(arg1, arg2) != 0;
                *pos += 3;
            } else if (strcmp(op, "-eq") == 0 || strcmp(op, "-ne") == 0 ||
                       strcmp(op, "-lt") == 0 || strcmp(op, "-le") == 0 ||
                       strcmp(op, "-gt") == 0 || strcmp(op, "-ge") == 0) {
                char *endptr1, *endptr2;
                long num1 = strtol(arg1, &endptr1, 10);
                long num2 = strtol(arg2, &endptr2, 10);
                if (*endptr1 != '\0' || *endptr2 != '\0') {
                    fprintf(stderr, "%s: %s: numeric argument required\n", args[0], *endptr1 ? arg1 : arg2);
                    executor_set_status(exec, 2);
                    return false;
                }
                if (strcmp(op, "-eq") == 0) left_result = num1 == num2;
                else if (strcmp(op, "-ne") == 0) left_result = num1 != num2;
                else if (strcmp(op, "-lt") == 0) left_result = num1 < num2;
                else if (strcmp(op, "-le") == 0) left_result = num1 <= num2;
                else if (strcmp(op, "-gt") == 0) left_result = num1 > num2;
                else left_result = num1 >= num2;
                *pos += 3;
            } else {
                // Invalid binary operator
                goto try_unary;
            }
        } else {
try_unary:
            // Unary operators: -n, -z, -f, etc.
            char *op = args[*pos];
            char *arg = args[*pos + 1];
            struct stat st;

            if (strcmp(op, "-n") == 0) {
                left_result = strlen(arg) > 0;
                *pos += 2;
            } else if (strcmp(op, "-z") == 0) {
                left_result = strlen(arg) == 0;
                *pos += 2;
            } else if (strcmp(op, "-b") == 0) {
                left_result = stat(arg, &st) == 0 && S_ISBLK(st.st_mode);
                *pos += 2;
            } else if (strcmp(op, "-c") == 0) {
                left_result = stat(arg, &st) == 0 && S_ISCHR(st.st_mode);
                *pos += 2;
            } else if (strcmp(op, "-d") == 0) {
                left_result = stat(arg, &st) == 0 && S_ISDIR(st.st_mode);
                *pos += 2;
            } else if (strcmp(op, "-e") == 0) {
                left_result = stat(arg, &st) == 0;
                *pos += 2;
            } else if (strcmp(op, "-f") == 0) {
                left_result = stat(arg, &st) == 0 && S_ISREG(st.st_mode);
                *pos += 2;
            } else if (strcmp(op, "-g") == 0) {
                left_result = stat(arg, &st) == 0 && (st.st_mode & S_ISGID);
                *pos += 2;
            } else if (strcmp(op, "-h") == 0 || strcmp(op, "-L") == 0) {
                left_result = lstat(arg, &st) == 0 && S_ISLNK(st.st_mode);
                *pos += 2;
            } else if (strcmp(op, "-p") == 0) {
                left_result = stat(arg, &st) == 0 && S_ISFIFO(st.st_mode);
                *pos += 2;
            } else if (strcmp(op, "-r") == 0) {
                left_result = access(arg, R_OK) == 0;
                *pos += 2;
            } else if (strcmp(op, "-s") == 0) {
                left_result = stat(arg, &st) == 0 && st.st_size > 0;
                *pos += 2;
            } else if (strcmp(op, "-t") == 0) {
                char *endptr;
                long fd = strtol(arg, &endptr, 10);
                if (*endptr != '\0' || fd < 0) {
                    fprintf(stderr, "%s: %s: numeric argument required\n", args[0], arg);
                    executor_set_status(exec, 2);
                    return false;
                }
                left_result = isatty((int)fd) != 0;
                *pos += 2;
            } else if (strcmp(op, "-u") == 0) {
                left_result = stat(arg, &st) == 0 && (st.st_mode & S_ISUID);
                *pos += 2;
            } else if (strcmp(op, "-w") == 0) {
                left_result = access(arg, W_OK) == 0;
                *pos += 2;
            } else if (strcmp(op, "-x") == 0) {
                left_result = access(arg, X_OK) == 0;
                *pos += 2;
            } else {
                // Single argument case
                *pos -= 1;
                left_result = strlen(args[*pos]) > 0;
                (*pos)++;
            }
        }
    } else {
        // Single argument
        left_result = strlen(args[*pos]) > 0;
        (*pos)++;
    }

    // Handle -a and -o
    while (*pos < max_args) {
        if (strcmp(args[*pos], "-o") == 0) {
            (*pos)++;
            if (*pos >= max_args) {
                fprintf(stderr, "%s: binary operator expected\n", args[0]);
                executor_set_status(exec, 2);
                return false;
            }
            bool right_result = evaluate_expr(exec, args, pos, max_args);
            return left_result || right_result;
        } else if (strcmp(args[*pos], "-a") == 0) {
            (*pos)++;
            if (*pos >= max_args) {
                fprintf(stderr, "%s: binary operator expected\n", args[0]);
                executor_set_status(exec, 2);
                return false;
            }
            bool right_result = evaluate_expr(exec, args, pos, max_args);
            if (!left_result) return false; // Short-circuit
            left_result = right_result;
        } else {
            break; // Extra args handled by caller
        }
    }

    return left_result;
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
    bool clear_env = false;  // -c option
    bool login_shell = false; // -l option
    char *argv0_name = NULL; // -a name
    int optind = 1;
    char *command = NULL;
    int cmd_argc = 0;
    char **cmd_argv = NULL;

    // Parse options
    while (optind < argc && argv[optind][0] == '-' && argv[optind][1] != '\0') {
        if (strcmp(argv[optind], "-c") == 0) {
            clear_env = true;
            optind++;
        } else if (strcmp(argv[optind], "-l") == 0) {
            login_shell = true;
            optind++;
        } else if (strcmp(argv[optind], "-a") == 0) {
            if (optind + 1 >= argc) {
                fprintf(stderr, "exec: -a: option requires an argument\n");
                executor_set_status(exec, 2);
                if (!exec->is_interactive) exit(2);
                return EXEC_FAILURE;
            }
            argv0_name = argv[optind + 1];
            optind += 2;
        } else if (strcmp(argv[optind], "--") == 0) {
            optind++;
            break;
        } else {
            fprintf(stderr, "exec: %s: invalid option\n", argv[optind]);
            executor_set_status(exec, 2);
            if (!exec->is_interactive) exit(2);
            return EXEC_FAILURE;
        }
    }

    // Collect command and arguments
    if (optind < argc && argv[optind][0] != '>' && argv[optind][0] != '<' &&
        strcmp(argv[optind], ">>") != 0 && strcmp(argv[optind], ">&") != 0 &&
        strcmp(argv[optind], "<&") != 0 && strcmp(argv[optind], "<>") != 0) {
        command = argv[optind];
        cmd_argc = 0;
        cmd_argv = malloc(sizeof(char *) * (argc - optind + 1));
        if (!cmd_argv) {
            fprintf(stderr, "exec: memory allocation failed\n");
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1);
            return EXEC_FAILURE;
        }
        for (int i = optind; i < argc; i++) {
            // Stop at redirection operators
            if (argv[i][0] == '>' || argv[i][0] == '<' ||
                strcmp(argv[i], ">>") == 0 || strcmp(argv[i], ">&") == 0 ||
                strcmp(argv[i], "<&") == 0 || strcmp(argv[i], "<>") == 0) {
                break;
            }
            cmd_argv[cmd_argc++] = argv[i];
        }
        cmd_argv[cmd_argc] = NULL;
        optind += cmd_argc;
    }

    // Parse and apply redirections
    while (optind < argc) {
        Redirect redir = {0};
        bool valid = false;
        char *io_number = NULL;
        char *target = NULL;

        // Check for io_number (e.g., 2>)
        if (optind + 1 < argc && isdigit(argv[optind][0])) {
            io_number = argv[optind];
            optind++;
        }

        // Identify redirection operator
        if (optind >= argc) {
            fprintf(stderr, "exec: missing redirection target\n");
            free(cmd_argv);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1);
            return EXEC_FAILURE;
        }

        if (strcmp(argv[optind], ">") == 0) {
            redir.operation = GREAT;
            valid = true;
        } else if (strcmp(argv[optind], ">>") == 0) {
            redir.operation = DGREAT;
            valid = true;
        } else if (strcmp(argv[optind], "<") == 0) {
            redir.operation = LESS;
            valid = true;
        } else if (strcmp(argv[optind], ">&") == 0) {
            redir.operation = GREATAND;
            valid = true;
        } else if (strcmp(argv[optind], "<&") == 0) {
            redir.operation = LESSAND;
            valid = true;
        } else if (strcmp(argv[optind], "<>") == 0) {
            redir.operation = LESSGREAT;
            valid = true;
        }

        if (!valid) {
            fprintf(stderr, "exec: %s: invalid redirection\n", argv[optind]);
            free(cmd_argv);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1);
            return EXEC_FAILURE;
        }

        optind++;
        if (optind >= argc) {
            fprintf(stderr, "exec: %s: missing redirection target\n", argv[optind - 1]);
            free(cmd_argv);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1);
            return EXEC_FAILURE;
        }

        redir.io_number = io_number;
        redir.filename = argv[optind];
        if (apply_redirect(&redir) != 0) {
            fprintf(stderr, "exec: %s: %s\n", redir.filename, strerror(errno));
            free(cmd_argv);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1);
            return EXEC_FAILURE;
        }
        optind++;
    }

    // No command: Apply redirections to current shell
    if (!command) {
        free(cmd_argv);
        executor_set_status(exec, 0);
        return EXEC_SUCCESS;
    }

    // Command given: Replace shell
    if (clear_env) {
        clearenv();
    }

    if (login_shell && cmd_argv[0]) {
        char *dash_cmd = malloc(strlen(cmd_argv[0]) + 2);
        if (!dash_cmd) {
            fprintf(stderr, "exec: memory allocation failed\n");
            free(cmd_argv);
            executor_set_status(exec, 1);
            if (!exec->is_interactive) exit(1);
            return EXEC_FAILURE;
        }
        sprintf(dash_cmd, "-%s", cmd_argv[0]);
        cmd_argv[0] = dash_cmd;
    }

    if (argv0_name && cmd_argv[0]) {
        cmd_argv[0] = argv0_name;
    }

    execvp(command, cmd_argv);
    fprintf(stderr, "exec: %s: %s\n", command, strerror(errno));
    free(cmd_argv);
    if (login_shell && cmd_argv[0]) free(cmd_argv[0]); // Free dash_cmd
    executor_set_status(exec, errno == ENOENT ? 127 : 1);
    if (!exec->is_interactive) exit(errno == ENOENT ? 127 : 1);
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
    int optind = 1;
    bool set_options = false;
    bool unset_options = false;

    // Parse options
    while (optind < argc && argv[optind][0] == '-' && argv[optind][1] != '\0') {
        if (strcmp(argv[optind], "--") == 0) {
            optind++;
            break;
        }
        for (int i = 1; argv[optind][i]; i++) {
            switch (argv[optind][i]) {
                case 'e':
                    exec->errexit = true;
                    break;
                case 'u':
                    exec->nounset = true;
                    break;
                case 'x':
                    exec->xtrace = true;
                    break;
                default:
                    fprintf(stderr, "set: -%c: invalid option\n", argv[optind][i]);
                    executor_set_status(exec, 2);
                    if (!exec->is_interactive) exit(2);
                    return EXEC_FAILURE;
            }
        }
        optind++;
    }

    // Handle +e, +u, +x
    while (optind < argc && argv[optind][0] == '+' && argv[optind][1] != '\0') {
        for (int i = 1; argv[optind][i]; i++) {
            switch (argv[optind][i]) {
                case 'e':
                    exec->errexit = false;
                    break;
                case 'u':
                    exec->nounset = false;
                    break;
                case 'x':
                    exec->xtrace = false;
                    break;
                default:
                    fprintf(stderr, "set: +%c: invalid option\n", argv[optind][i]);
                    executor_set_status(exec, 2);
                    if (!exec->is_interactive) exit(2);
                    return EXEC_FAILURE;
            }
        }
        optind++;
    }

    // Handle -o, +o
    if (optind < argc && strcmp(argv[optind], "-o") == 0) {
        optind++;
        if (optind >= argc) {
            // Display options
            printf("errexit\t%s\n", exec->errexit ? "on" : "off");
            printf("nounset\t%s\n", exec->nounset ? "on" : "off");
            printf("xtrace\t%s\n", exec->xtrace ? "on" : "off");
            executor_set_status(exec, 0);
            return EXEC_SUCCESS;
        }
        if (strcmp(argv[optind], "errexit") == 0) {
            exec->errexit = true;
        } else if (strcmp(argv[optind], "nounset") == 0) {
            exec->nounset = true;
        } else if (strcmp(argv[optind], "xtrace") == 0) {
            exec->xtrace = true;
        } else {
            fprintf(stderr, "set: -o %s: invalid option\n", argv[optind]);
            executor_set_status(exec, 2);
            if (!exec->is_interactive) exit(2);
            return EXEC_FAILURE;
        }
        optind++;
    } else if (optind < argc && strcmp(argv[optind], "+o") == 0) {
        optind++;
        if (optind >= argc) {
            // Scriptable output
            if (exec->errexit) printf("set -o errexit\n");
            if (exec->nounset) printf("set -o nounset\n");
            if (exec->xtrace) printf("set -o xtrace\n");
            executor_set_status(exec, 0);
            return EXEC_SUCCESS;
        }
        if (strcmp(argv[optind], "errexit") == 0) {
            exec->errexit = false;
        } else if (strcmp(argv[optind], "nounset") == 0) {
            exec->nounset = false;
        } else if (strcmp(argv[optind], "xtrace") == 0) {
            exec->xtrace = false;
        } else {
            fprintf(stderr, "set: +o %s: invalid option\n", argv[optind]);
            executor_set_status(exec, 2);
            if (!exec->is_interactive) exit(2);
            return EXEC_FAILURE;
        }
        optind++;
    }

    // Set positional parameters
    if (optind < argc) {
        ptr_array_clear(exec->vars->positional_params);
        for (int i = optind; i < argc; i++) {
            ptr_array_append(exec->vars->positional_params, strdup(argv[i]));
        }
    } else if (optind == 1) {
        // No args or options: dump variables
        variable_store_dump_variables(exec->vars);
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
        { "cd", builtin_cd },
        { "test", builtin_test },
        { "[", builtin_test },
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
