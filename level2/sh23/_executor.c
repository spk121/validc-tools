#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
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
