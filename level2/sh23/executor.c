#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include "executor.h"
#include "string.h"

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
    exec->is_interactive = true;
    exec->errexit = false;
    exec->nounset = false;
    exec->xtrace = false;
    trap_store_set_executor(exec);
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
    trap_store_set_executor(NULL);
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
                int index = atoi(exp->data.parameter.name);
                if (index >= 1 && index <= exec->vars->positional_params->len) {
                    string_append_zstring(result, (char *)exec->vars->positional_params->data[index - 1]);
                }
                break;
            }
            case EXPANSION_SPECIAL: {
                char buf[32];
                switch (exp->data.special.param) {
                    case SPECIAL_STAR:
                        for (int j = 0; j < exec->vars->positional_params->len; j++) {
                            string_append_zstring(result, (char *)exec->vars->positional_params->data[j]);
                            if (j < exec->vars->positional_params->len - 1) {
                                string_append_zstring(result, " ");
                            }
                        }
                        break;
                    case SPECIAL_AT:
                        for (int j = 0; j < exec->vars->positional_params->len; j++) {
                            string_append_zstring(result, (char *)exec->vars->positional_params->data[j]);
                            if (j < exec->vars->positional_params->len - 1) {
                                string_append_zstring(result, " ");
                            }
                        }
                        break;
                    case SPECIAL_HASH:
                        snprintf(buf, sizeof(buf), "%d", exec->vars->positional_params->len);
                        string_append_zstring(result, buf);
                        break;
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
                    case SPECIAL_HYPHEN:
                        string_append_zstring(result, exec->vars->options);
                        break;
                    case SPECIAL_DOLLAR:
                        snprintf(buf, sizeof(buf), "%ld", exec->vars->pid);
                        string_append_zstring(result, buf);
                        break;
                    case SPECIAL_ZERO:
                        string_append_zstring(result, exec->vars->shell_name);
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
            case EXPANSION_DEFAULT: {
                const char *value = variable_store_default_value(
                    exec->vars,
                    exp->data.default_exp.var,
                    exp->data.default_exp.default_value
                );
                if (value) {
                    string_append_zstring(result, value);
                    free((void *)value);
                }
                break;
            }
            case EXPANSION_ASSIGN: {
                const char *value = variable_store_assign_default(
                    exec->vars,
                    exp->data.default_exp.var,
                    exp->data.default_exp.default_value
                );
                if (value) {
                    string_append_zstring(result, value);
                    free((void *)value);
                }
                break;
            }
            case EXPANSION_ERROR_IF_UNSET: {
                const char *value = variable_store_indicate_error(
                    exec->vars,
                    exp->data.default_exp.var,
                    exp->data.default_exp.default_value
                );
                if (value) {
                    string_append_zstring(result, value);
                    free((void *)value);
                } else {
                    executor_set_status(exec, 1);
                    if (!exec->is_interactive) exit(1);
                }
                break;
            }
            case EXPANSION_ALTERNATIVE: {
                const char *value = variable_store_alternative_value(
                    exec->vars,
                    exp->data.default_exp.var,
                    exp->data.default_exp.default_value
                );
                if (value) {
                    string_append_zstring(result, value);
                    free((void *)value);
                }
                break;
            }
            case EXPANSION_LENGTH: {
                size_t len = variable_store_length(exec->vars, exp->data.length.var);
                char buf[32];
                snprintf(buf, sizeof(buf), "%zu", len);
                string_append_zstring(result, buf);
                break;
            }
            case EXPANSION_PREFIX_SHORT: {
                const char *value = variable_store_remove_prefix(
                    exec->vars,
                    exp->data.pattern.var,
                    exp->data.pattern.pattern,
                    false
                );
                if (value) {
                    string_append_zstring(result, value);
                    free((void *)value);
                }
                break;
            }
            case EXPANSION_PREFIX_LONG: {
                const char *value = variable_store_remove_prefix(
                    exec->vars,
                    exp->data.pattern.var,
                    exp->data.pattern.pattern,
                    true
                );
                if (value) {
                    string_append_zstring(result, value);
                    free((void *)value);
                }
                break;
            }
            case EXPANSION_SUFFIX_SHORT: {
                const char *value = variable_store_remove_suffix(
                    exec->vars,
                    exp->data.pattern.var,
                    exp->data.pattern.pattern,
                    false
                );
                if (value) {
                    string_append_zstring(result, value);
                    free((void *)value);
                }
                break;
            }
            case EXPANSION_SUFFIX_LONG: {
                const char *value = variable_store_remove_suffix(
                    exec->vars,
                    exp->data.pattern.var,
                    exp->data.pattern.pattern,
                    true
                );
                if (value) {
                    string_append_zstring(result, value);
                    free((void *)value);
                }
                break;
            }
            case EXPANSION_ARITHMETIC: {
                ArithmeticResult arith = arithmetic_evaluate(exec, exp->data.arithmetic.expression);
                if (arith.failed) {
                    fprintf(stderr, "arithmetic: %s\n", arith.error);
                    executor_set_status(exec, 1);
                    arithmetic_result_free(&arith);
                    break;
                }
                char buf[32];
                snprintf(buf, sizeof(buf), "%ld", arith.value);
                string_append_zstring(result, buf);
                arithmetic_result_free(&arith);
                break;
            }
            case EXPANSION_TILDE: {
                if (!exp->data.tilde.user || strcmp(exp->data.tilde.user, "") == 0) {
                    const char *home = variable_store_get_variable(exec->vars, "HOME");
                    if (home) {
                        string_append_zstring(result, home);
                    }
                } else {
                    struct passwd *pw = getpwnam(exp->data.tilde.user);
                    if (pw) {
                        string_append_zstring(result, pw->pw_dir);
                    }
                }
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

    if (exec->xtrace && expanded->len > 0) {
        fprintf(stderr, "+");
        for (int i = 0; i < expanded->len; i++) {
            fprintf(stderr, " %s", (char *)expanded->data[i]);
        }
        fprintf(stderr, "\n");
    }

    // Check for builtins
    if (argv && argv[0]) {
        ExecStatus ret = builtin_execute(exec, argv, argc);
        if (ret != EXEC_NOT_BUILTIN) {
            return ret;
        }
        // argv freed by builtin_execute
    } else {
        executor_set_status(exec, 0);
        free(argv);
        return EXEC_SUCCESS;
    }

    // Check for function
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

    // Execute external command
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

// Execute AND_OR node
static ExecStatus exec_and_or(Executor *exec, ASTNode *ast) {
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

// Execute LIST node
static ExecStatus exec_list(Executor *exec, ASTNode *ast) {
    ExecStatus ret = executor_run(exec, ast->data.list.and_or);
    if (ret == EXEC_RETURN) return ret;
    if (ret != EXEC_SUCCESS) return ret;
    if (ast->data.list.next && ast->data.list.separator != TOKEN_AMP) {
        return executor_run(exec, ast->data.list.next);
    }
    return EXEC_SUCCESS;
}

// Execute IF_CLAUSE node
static ExecStatus exec_if_clause(Executor *exec, ASTNode *ast) {
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

// Execute FOR_CLAUSE node
static ExecStatus exec_for_clause(Executor *exec, ASTNode *ast) {
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

// Execute WHILE_CLAUSE node
static ExecStatus exec_while_clause(Executor *exec, ASTNode *ast) {
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

// Execute UNTIL_CLAUSE node
static ExecStatus exec_until_clause(Executor *exec, ASTNode *ast) {
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
        if (executor_get_status(exec) == 0) break;
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

// Execute CASE_CLAUSE node
static ExecStatus exec_case_clause(Executor *exec, ASTNode *ast) {
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

// Execute SUBSHELL node
static ExecStatus exec_subshell(Executor *exec, ASTNode *ast) {
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

// Execute FUNCTION_DEFINITION node
static ExecStatus exec_function_definition(Executor *exec, ASTNode *ast) {
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

// Execute AST node
ExecStatus executor_run(Executor *exec, ASTNode *ast) {
    if (!ast) {
        executor_set_status(exec, 0);
        return EXEC_SUCCESS;
    }

    ExecStatus ret = EXEC_SUCCESS;

    switch (ast->type) {
        case AST_SIMPLE_COMMAND:
            ret = exec_simple_command(exec, ast);
            break;
        case AST_PIPELINE:
            ret = exec_pipeline(exec, ast);
            break;
        case AST_AND_OR:
            ret = exec_and_or(exec, ast);
            break;
        case AST_LIST:
            ret = exec_list(exec, ast);
            break;
        case AST_COMPLETE_COMMAND:
            ret = executor_run(exec, ast->data.complete_command.list);
            break;
        case AST_PROGRAM:
            ret = executor_run(exec, ast->data.program.commands);
            break;
        case AST_IF_CLAUSE:
            ret = exec_if_clause(exec, ast);
            break;
        case AST_FOR_CLAUSE:
            ret = exec_for_clause(exec, ast);
            break;
        case AST_WHILE_CLAUSE:
            ret = exec_while_clause(exec, ast);
            break;
        case AST_UNTIL_CLAUSE:
            ret = exec_until_clause(exec, ast);
            break;
        case AST_CASE_CLAUSE:
            ret = exec_case_clause(exec, ast);
            break;
        case AST_BRACE_GROUP: {
            ret = executor_run(exec, ast->data.brace_group.body);
            break;
        }
        case AST_SUBSHELL:
            ret = exec_subshell(exec, ast);
            break;
        case AST_FUNCTION_DEFINITION:
            ret = exec_function_definition(exec, ast);
            break;
        case AST_IO_REDIRECT:
            ret = EXEC_SUCCESS;
            break;
        case AST_EXPANSION:
            // Expansions are handled in expand_string during AST_SIMPLE_COMMAND
            ret = EXEC_SUCCESS;
            break;
        default:
            executor_set_status(exec, 1);
            ret =  EXEC_FAILURE;
            break;
    }
    
    // errexit: Exit on non-zero status, skip in conditionals
    if (exec->errexit && exec->last_status != 0 && !is_conditional &&
        ret != EXEC_RETURN && ret != EXEC_BREAK && ret != EXEC_CONTINUE) {
        if (!exec->is_interactive) {
            exit(exec->last_status);
        }
    }

    return ret;
}
