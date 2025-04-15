#ifndef STRING_H
#define STRING_H

#include <stddef.h>

typedef struct String String;

// Create and destroy
String *string_create(const char *data);
String *string_create_empty(size_t capacity);
String *string_create_from(String *other);
void string_destroy(String *str);

// Accessors
const char *string_data(const String *str);
size_t string_length(const String *str);
size_t string_capacity(const String *str);
int string_is_empty(const String *str);

// Modification
int string_append(String *str, const char *data);
int string_append_string(String *str, const String *other);
int string_clear(String *str);
int string_set(String *str, const char *data);
int string_resize(String *str, size_t new_capacity);

// Operations
String *string_substring(const String *str, size_t start, size_t length);
int string_compare(const String *str1, const String *str2);
int string_compare_cstr(const String *str, const char *data);
int string_find(const String *str, const char *substr, size_t *pos);
int string_replace(String *str, const char *find, const char *replace, size_t max_replacements);

// UTF-8 specific
size_t string_utf8_length(const String *str);
int string_is_valid_utf8(const String *str);
int string_utf8_char_at(const String *str, size_t char_index, char *buffer, size_t buffer_size);

#endif
