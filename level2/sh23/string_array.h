#ifndef STRING_ARRAY_H
#define STRING_ARRAY_H

#include <stddef.h>
#include "string.h"
/* INCLUDE2 */

typedef struct StringArray StringArray;

// Function pointer types
typedef void (*StringArrayFreeFunc)(String * element);
typedef void (*StringArrayApplyFunc)(String * element, void *user_data);
typedef int (*StringArrayCompareFunc)(const String * element, const void *user_data);

// Create and destroy
StringArray *string_array_create(void);
StringArray *string_array_create_with_free(StringArrayFreeFunc free_func);
void string_array_destroy(StringArray *array);

// Accessors
size_t string_array_size(const StringArray *array);
size_t string_array_capacity(const StringArray *array);
String * string_array_get(const StringArray *array, size_t index);
int string_array_is_empty(const StringArray *array);

// Modification
int string_array_append(StringArray *array, String * element);
int string_array_set(StringArray *array, size_t index, String * element);
int string_array_remove(StringArray *array, size_t index);
int string_array_clear(StringArray *array);
int string_array_resize(StringArray *array, size_t new_capacity);

// Operations
void string_array_foreach(StringArray *array, StringArrayApplyFunc apply_func, void *user_data);
int string_array_find(StringArray *array, String * element, size_t *index);
int string_array_find_with_compare(StringArray *array, const void *data, StringArrayCompareFunc compare_func, size_t *index);

#endif
