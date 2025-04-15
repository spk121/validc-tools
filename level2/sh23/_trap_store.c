#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "trap_store.h"
#include "tokenizer.h"
#include "parser.h"

// Global executor for signal handler
static Executor *global_executor = NULL;

TrapStore *trap_store_create(void) {
    TrapStore *store = malloc(sizeof(TrapStore));
    if (!store) return NULL;
    store->traps = ptr_array_create();
    if (!store->traps) {
        free(store);
        return NULL;
    }
    return store;
}

void trap_store_destroy(TrapStore *store) {
    if (!store) return;
    for (int i = 0; i < store->traps->len; i++) {
        Trap *trap = store->traps->data[i];
        string_destroy(trap->action);
        free(trap);
    }
    ptr_array_destroy(store->traps);
    free(store);
}

void trap_store_set(TrapStore *store, int signal, const char *action) {
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

Trap *trap_store_get(TrapStore *store, int signal) {
    for (int i = 0; i < store->traps->len; i++) {
        Trap *trap = store->traps->data[i];
        if (trap->signal == signal) {
            return trap;
        }
    }
    return NULL;
}

void trap_store_print(TrapStore *store) {
    for (int i = 0; i < store->traps->len; i++) {
        Trap *trap = store->traps->data[i];
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

void trap_store_set_executor(Executor *exec) {
    global_executor = exec;
}
