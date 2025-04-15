

#ifndef PTR_ARRAY_H
#define PTR_ARRAY_H

#include <stddef.h>

typedef struct PtrArray PtrArray;

// Function pointer types
typedef void (*PtrArrayFreeFunc)(void *element);
typedef void (*PtrArrayApplyFunc)(void *element, void *user_data);

// Create and destroy
PtrArray *ptr_array_create(void);
PtrArray *ptr_array_create_with_free(PtrArrayFreeFunc free_func);
void ptr_array_destroy(PtrArray *array);

// Accessors
size_t ptr_array_size(const PtrArray *array);
size_t ptr_array_capacity(const PtrArray *array);
void *ptr_array_get(const PtrArray *array, size_t index);
int ptr_array_is_empty(const PtrArray *array);

// Modification
int ptr_array_append(PtrArray *array, void *element);
int ptr_array_set(PtrArray *array, size_t index, void *element);
int ptr_array_remove(PtrArray *array, size_t index);
int ptr_array_clear(PtrArray *array);
int ptr_array_resize(PtrArray *array, size_t new_capacity);

// Operations
void ptr_array_foreach(PtrArray *array, PtrArrayApplyFunc apply_func, void *user_data);
int ptr_array_find(PtrArray *array, void *element, size_t *index);

#endif
