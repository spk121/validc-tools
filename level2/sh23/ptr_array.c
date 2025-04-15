#include "ptr_array.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16
#define GROW_FACTOR 2

struct PtrArray {
    void **elements;          // Array of pointers
    size_t size;             // Number of elements
    size_t capacity;         // Total allocated slots
    PtrArrayFreeFunc free_func; // Optional cleanup function
};

// Helper: Ensure capacity
static int ptr_array_ensure_capacity(PtrArray *array, size_t needed) {
    if (!array || needed < 0) return -1;
    if (needed <= array->capacity) return 0;
    
    size_t new_capacity = array->capacity ? array->capacity : INITIAL_CAPACITY;
    while (new_capacity < needed) new_capacity *= GROW_FACTOR;
    
    void **new_elements = realloc(array->elements, new_capacity * sizeof(void *));
    if (!new_elements) return -1;
    
    array->elements = new_elements;
    array->capacity = new_capacity;
    return 0;
}

// Create and destroy
PtrArray *ptr_array_create(void) {
    return ptr_array_create_with_free(NULL);
}

PtrArray *ptr_array_create_with_free(PtrArrayFreeFunc free_func) {
    PtrArray *array = malloc(sizeof(PtrArray));
    if (!array) return NULL;
    
    array->elements = NULL;
    array->size = 0;
    array->capacity = 0;
    array->free_func = free_func;
    
    return array;
}

void ptr_array_destroy(PtrArray *array) {
    if (!array) return;
    
    if (array->free_func) {
        for (size_t i = 0; i < array->size; i++) {
            if (array->elements[i]) {
                array->free_func(array->elements[i]);
            }
        }
    }
    
    free(array->elements);
    free(array);
}

// Accessors
size_t ptr_array_size(const PtrArray *array) {
    return array ? array->size : 0;
}

size_t ptr_array_capacity(const PtrArray *array) {
    return array ? array->capacity : 0;
}

void *ptr_array_get(const PtrArray *array, size_t index) {
    if (!array || index >= array->size) return NULL;
    return array->elements[index];
}

int ptr_array_is_empty(const PtrArray *array) {
    return !array || array->size == 0;
}

// Modification
int ptr_array_append(PtrArray *array, void *element) {
    if (!array) return -1;
    
    if (ptr_array_ensure_capacity(array, array->size + 1) != 0) return -1;
    
    array->elements[array->size] = element;
    array->size++;
    return 0;
}

int ptr_array_set(PtrArray *array, size_t index, void *element) {
    if (!array || index >= array->size) return -1;
    
    if (array->free_func && array->elements[index]) {
        array->free_func(array->elements[index]);
    }
    
    array->elements[index] = element;
    return 0;
}

int ptr_array_remove(PtrArray *array, size_t index) {
    if (!array || index >= array->size) return -1;
    
    if (array->free_func && array->elements[index]) {
        array->free_func(array->elements[index]);
    }
    
    // Shift elements
    for (size_t i = index; i < array->size - 1; i++) {
        array->elements[i] = array->elements[i + 1];
    }
    
    array->size--;
    array->elements[array->size] = NULL;
    return 0;
}

int ptr_array_clear(PtrArray *array) {
    if (!array) return -1;
    
    if (array->free_func) {
        for (size_t i = 0; i < array->size; i++) {
            if (array->elements[i]) {
                array->free_func(array->elements[i]);
            }
        }
    }
    
    array->size = 0;
    return 0;
}

int ptr_array_resize(PtrArray *array, size_t new_capacity) {
    if (!array) return -1;
    
    if (new_capacity < array->size) {
        // Free truncated elements
        if (array->free_func) {
            for (size_t i = new_capacity; i < array->size; i++) {
                if (array->elements[i]) {
                    array->free_func(array->elements[i]);
                }
            }
        }
        array->size = new_capacity;
    }
    
    if (new_capacity == 0) {
        free(array->elements);
        array->elements = NULL;
        array->capacity = 0;
        return 0;
    }
    
    void **new_elements = realloc(array->elements, new_capacity * sizeof(void *));
    if (!new_elements) return -1;
    
    array->elements = new_elements;
    array->capacity = new_capacity;
    
    // Null out new slots if growing
    for (size_t i = array->size; i < array->capacity; i++) {
        array->elements[i] = NULL;
    }
    
    return 0;
}

// Operations
void ptr_array_foreach(PtrArray *array, PtrArrayApplyFunc apply_func, void *user_data) {
    if (!array || !apply_func) return;
    
    for (size_t i = 0; i < array->size; i++) {
        if (array->elements[i]) {
            apply_func(array->elements[i], user_data);
        }
    }
}

int ptr_array_find(PtrArray *array, void *element, size_t *index) {
    if (!array || !index) return -1;
    
    for (size_t i = 0; i < array->size; i++) {
        if (array->elements[i] == element) {
            *index = i;
            return 0;
        }
    }
    
    return -1;
}
