#ifndef PTR_ARRAY_MACROS_H
#define PTR_ARRAY_MACROS_H

// Helper macros for concatenation
#define CONCAT(a, b) a##b
#define CONCAT3(a, b, c) a##b##c

// Define array class and function names
#define ARRAY_CLASS(type_prefix) CONCAT(type_prefix, Array)
#define ARRAY_FUNC(type_prefix, func) CONCAT3(type_prefix, _array_, func)

// Define function pointer types
#define ARRAY_FREE_FUNC(type_prefix) CONCAT(ARRAY_CLASS(type_prefix), FreeFunc)
#define ARRAY_APPLY_FUNC(type_prefix) CONCAT(ARRAY_CLASS(type_prefix), ApplyFunc)
#define ARRAY_COMPARE_FUNC(type_prefix) CONCAT(ARRAY_CLASS(type_prefix), CompareFunc)

// Define the header content
#define DEFINE_ARRAY_HEADER(type_prefix, element_type) \
    \
    typedef struct ARRAY_CLASS(type_prefix) ARRAY_CLASS(type_prefix); \
    \
    typedef void (*ARRAY_FREE_FUNC(type_prefix))(element_type element); \
    typedef void (*ARRAY_APPLY_FUNC(type_prefix))(element_type element, void *user_data); \
    typedef int (*ARRAY_COMPARE_FUNC(type_prefix))(const element_type element, const void *user_data); \
    \
    ARRAY_CLASS(type_prefix) *ARRAY_FUNC(type_prefix, create)(void); \
    ARRAY_CLASS(type_prefix) *ARRAY_FUNC(type_prefix, create_with_free)(ARRAY_FREE_FUNC(type_prefix) free_func); \
    void ARRAY_FUNC(type_prefix, destroy)(ARRAY_CLASS(type_prefix) *array); \
    \
    size_t ARRAY_FUNC(type_prefix, size)(const ARRAY_CLASS(type_prefix) *array); \
    size_t ARRAY_FUNC(type_prefix, capacity)(const ARRAY_CLASS(type_prefix) *array); \
    element_type ARRAY_FUNC(type_prefix, get)(const ARRAY_CLASS(type_prefix) *array, size_t index); \
    int ARRAY_FUNC(type_prefix, is_empty)(const ARRAY_CLASS(type_prefix) *array); \
    \
    int ARRAY_FUNC(type_prefix, append)(ARRAY_CLASS(type_prefix) *array, element_type element); \
    int ARRAY_FUNC(type_prefix, set)(ARRAY_CLASS(type_prefix) *array, size_t index, element_type element); \
    int ARRAY_FUNC(type_prefix, remove)(ARRAY_CLASS(type_prefix) *array, size_t index); \
    int ARRAY_FUNC(type_prefix, clear)(ARRAY_CLASS(type_prefix) *array); \
    int ARRAY_FUNC(type_prefix, resize)(ARRAY_CLASS(type_prefix) *array, size_t new_capacity); \
    \
    void ARRAY_FUNC(type_prefix, foreach)(ARRAY_CLASS(type_prefix) *array, ARRAY_APPLY_FUNC(type_prefix) apply_func, void *user_data); \
    int ARRAY_FUNC(type_prefix, find)(ARRAY_CLASS(type_prefix) *array, element_type element, size_t *index); \
    int ARRAY_FUNC(type_prefix, find_with_compare)(ARRAY_CLASS(type_prefix) *array, const void *data, ARRAY_COMPARE_FUNC(type_prefix) compare_func, size_t *index);

// Define the implementation content
#define DEFINE_ARRAY_IMPLEMENTATION(type_prefix, element_type) \
    \
    struct ARRAY_CLASS(type_prefix) { \
        element_type *data; \
        size_t size; \
        size_t capacity; \
        ARRAY_FREE_FUNC(type_prefix) free_func; \
    }; \
    \
    static int ARRAY_FUNC(type_prefix, ensure_capacity)(ARRAY_CLASS(type_prefix) *array, size_t needed) \
    { \
        return_val_if_null(array, -1); \
        return_val_if_lt(needed, 0, -1); \
        \
        if (needed <= array->capacity) \
            return 0; \
        \
        size_t new_capacity = array->capacity ? array->capacity : INITIAL_CAPACITY; \
        while (new_capacity < needed) \
            new_capacity *= GROW_FACTOR; \
        \
        element_type *new_data = realloc(array->data, new_capacity * sizeof(element_type)); \
        if (!new_data) { \
            log_fatal(STRINGIFY(ARRAY_FUNC(type_prefix, ensure_capacity)) ": memory allocation failure"); \
            return -1; \
        } \
        \
        array->data = new_data; \
        array->capacity = new_capacity; \
        return 0; \
    } \
    \
    ARRAY_CLASS(type_prefix) *ARRAY_FUNC(type_prefix, create)(void) \
    { \
        return ARRAY_FUNC(type_prefix, create_with_free)(NULL); \
    } \
    \
    ARRAY_CLASS(type_prefix) *ARRAY_FUNC(type_prefix, create_with_free)(ARRAY_FREE_FUNC(type_prefix) free_func) \
    { \
        ARRAY_CLASS(type_prefix) *array = malloc(sizeof(ARRAY_CLASS(type_prefix))); \
        if (!array) { \
            log_fatal(STRINGIFY(ARRAY_FUNC(type_prefix, create_with_free)) ": out of memory"); \
            return NULL; \
        } \
        \
        array->data = NULL; \
        array->size = 0; \
        array->capacity = 0; \
        array->free_func = free_func; \
        \
        if (ARRAY_FUNC(type_prefix, ensure_capacity)(array, INITIAL_CAPACITY) != 0) { \
            free(array); \
            log_fatal(STRINGIFY(ARRAY_FUNC(type_prefix, create_with_free)) ": out of memory"); \
            return NULL; \
        } \
        \
        return array; \
    } \
    \
    void ARRAY_FUNC(type_prefix, destroy)(ARRAY_CLASS(type_prefix) *array) \
    { \
        if (array) { \
            log_debug(STRINGIFY(ARRAY_FUNC(type_prefix, destroy)) ": freeing array %p, size %zu", array, array->size); \
            if (array->free_func) { \
                for (size_t i = 0; i < array->size; i++) { \
                    if (array->data[i]) { \
                        array->free_func(array->data[i]); \
                    } \
                } \
            } \
            free(array->data); \
            free(array); \
        } \
    } \
    \
    size_t ARRAY_FUNC(type_prefix, size)(const ARRAY_CLASS(type_prefix) *array) \
    { \
        return_val_if_null(array, 0); \
        return array->size; \
    } \
    \
    size_t ARRAY_FUNC(type_prefix, capacity)(const ARRAY_CLASS(type_prefix) *array) \
    { \
        return_val_if_null(array, 0); \
        return array->capacity; \
    } \
    \
    element_type ARRAY_FUNC(type_prefix, get)(const ARRAY_CLASS(type_prefix) *array, size_t index) \
    { \
        return_val_if_null(array, NULL); \
        return_val_if_ge(index, array->size, NULL); \
        return array->data[index]; \
    } \
    \
    int ARRAY_FUNC(type_prefix, is_empty)(const ARRAY_CLASS(type_prefix) *array) \
    { \
        return_val_if_null(array, 1); \
        return array->size == 0; \
    } \
    \
    int ARRAY_FUNC(type_prefix, append)(ARRAY_CLASS(type_prefix) *array, element_type element) \
    { \
        return_val_if_null(array, -1); \
        if (ARRAY_FUNC(type_prefix, ensure_capacity)(array, array->size + 1) != 0) { \
            log_fatal(STRINGIFY(ARRAY_FUNC(type_prefix, append)) ": out of memory"); \
            return -1; \
        } \
        \
        array->data[array->size] = element; \
        array->size++; \
        return 0; \
    } \
    \
    int ARRAY_FUNC(type_prefix, set)(ARRAY_CLASS(type_prefix) *array, size_t index, element_type element) \
    { \
        return_val_if_null(array, -1); \
        return_val_if_ge(index, array->size, -1); \
        \
        if (array->free_func && array->data[index]) { \
            array->free_func(array->data[index]); \
        } \
        array->data[index] = element; \
        return 0; \
    } \
    \
    int ARRAY_FUNC(type_prefix, remove)(ARRAY_CLASS(type_prefix) *array, size_t index) \
    { \
        return_val_if_null(array, -1); \
        return_val_if_ge(index, array->size, -1); \
        \
        if (array->free_func && array->data[index]) { \
            array->free_func(array->data[index]); \
        } \
        \
        for (size_t i = index; i < array->size - 1; i++) { \
            array->data[i] = array->data[i + 1]; \
        } \
        array->size--; \
        array->data[array->size] = NULL; \
        return 0; \
    } \
    \
    int ARRAY_FUNC(type_prefix, clear)(ARRAY_CLASS(type_prefix) *array) \
    { \
        return_val_if_null(array, -1); \
        \
        if (array->free_func) { \
            for (size_t i = 0; i < array->size; i++) { \
                if (array->data[i]) { \
                    array->free_func(array->data[i]); \
                } \
            } \
        } \
        array->size = 0; \
        for (size_t i = 0; i < array->capacity; i++) { \
            array->data[i] = NULL; \
        } \
        return 0; \
    } \
    \
    int ARRAY_FUNC(type_prefix, resize)(ARRAY_CLASS(type_prefix) *array, size_t new_capacity) \
    { \
        return_val_if_null(array, -1); \
        return_val_if_lt(new_capacity, 0, -1); \
        \
        if (new_capacity < array->size) { \
            if (array->free_func) { \
                for (size_t i = new_capacity; i < array->size; i++) { \
                    if (array->data[i]) { \
                        array->free_func(array->data[i]); \
                    } \
                } \
            } \
            array->size = new_capacity; \
        } \
        \
        if (ARRAY_FUNC(type_prefix, ensure_capacity)(array, new_capacity) != 0) { \
            log_fatal(STRINGIFY(ARRAY_FUNC(type_prefix, resize)) ": out of memory"); \
            return -1; \
        } \
        \
        for (size_t i = array->capacity; i < new_capacity; i++) { \
            array->data[i] = NULL; \
        } \
        array->capacity = new_capacity; \
        return 0; \
    } \
    \
    void ARRAY_FUNC(type_prefix, foreach)(ARRAY_CLASS(type_prefix) *array, ARRAY_APPLY_FUNC(type_prefix) apply_func, void *user_data) \
    { \
        if (!array || !apply_func) { \
            log_fatal(STRINGIFY(ARRAY_FUNC(type_prefix, foreach)) ": argument 'array' or 'apply_func' is null"); \
            return; \
        } \
        \
        for (size_t i = 0; i < array->size; i++) { \
            apply_func(array->data[i], user_data); \
        } \
    } \
    \
    int ARRAY_FUNC(type_prefix, find)(ARRAY_CLASS(type_prefix) *array, element_type element, size_t *index) \
    { \
        return_val_if_null(array, -1); \
        return_val_if_null(index, -1); \
        \
        for (size_t i = 0; i < array->size; i++) { \
            if (array->data[i] == element) { \
                *index = i; \
                return 0; \
            } \
        } \
        return -1; \
    } \
    \
    int ARRAY_FUNC(type_prefix, find_with_compare)(ARRAY_CLASS(type_prefix) *array, const void *data, ARRAY_COMPARE_FUNC(type_prefix) compare_func, size_t *index) \
    { \
        return_val_if_null(array, -1); \
        return_val_if_null(data, -1); \
        return_val_if_null(compare_func, -1); \
        return_val_if_null(index, -1); \
        \
        for (size_t i = 0; i < array->size; i++) { \
            if (compare_func(array->data[i], data) == 0) { \
                *index = i; \
                return 0; \
            } \
        } \
        return -1; \
    }

// Helper macro to stringify function names for logging
#define STRINGIFY(x) #x

#endif
