#ifndef FUNCTION_STORE_H
#define FUNCTION_STORE_H

#include "parser.h"
#include "ptr_array.h"

typedef struct {
    char *name;        // Function name
    ASTNode *body;     // AST node for function body (includes redirects)
} Function;

typedef struct {
    PtrArray *functions; // Array of Function*
} FunctionStore;

// Create and destroy function store
FunctionStore *function_store_create(void);
void function_store_destroy(FunctionStore *store);

// Add or update a function
void function_store_set(FunctionStore *store, const char *name, ASTNode *body);

// Get a function by name
Function *function_store_get(FunctionStore *store, const char *name);

#endif
