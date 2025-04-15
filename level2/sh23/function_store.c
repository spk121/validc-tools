#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "_function_store.h"

FunctionStore *function_store_create(void) {
    FunctionStore *store = malloc(sizeof(FunctionStore));
    if (!store) return NULL;
    store->functions = ptr_array_create();
    if (!store->functions) {
        free(store);
        return NULL;
    }
    return store;
}

void function_store_destroy(FunctionStore *store) {
    if (!store) return;
    for (int i = 0; i < ptr_array_size (store->functions); i++) {
        Function *func = ptr_array_get(store->functions, i);
        free(func->name);
        // Note: ASTNode is owned by parser, not freed here
        free(func);
    }
    ptr_array_destroy(store->functions);
    free(store);
}

void function_store_set(FunctionStore *store, const char *name, ASTNode *body) {
    if (!name || !*name || !is_valid_name_zstring(name)) {
        fprintf(stderr, "function_store_set: invalid function name: %s\n", name ? name : "");
        return;
    }

    // Check if function already exists
    for (int i = 0; i < ptr_array_size (store->functions); i++) {
        Function *func = ptr_array_get(store->functions, i);
            func->body = body;
            return;
        }
    }

    // Create new function
    Function *func = malloc(sizeof(Function));
    if (!func) return;
    func->name = strdup(name);
    if (!func->name) {
        free(func);
        return;
    }
    func->body = body;
    ptr_array_append(store->functions, func);
}

Function *function_store_get(FunctionStore *store, const char *name) {
    for (int i = 0; i < store->functions->len; i++) {
        Function *func = store->functions->data[i];
        if (strcmp(func->name, name) == 0) {
            return func;
        }
    }
    return NULL;
}