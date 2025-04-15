#include "string.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct String {
    char *data;     // UTF-8 encoded string
    size_t length;  // Byte length (excluding null terminator)
    size_t capacity;// Total allocated bytes (including null terminator)
};

#define INITIAL_CAPACITY 16
#define GROW_FACTOR 2

// Helper: Ensure capacity
static int string_ensure_capacity(String *str, size_t needed) {
    if (!str || needed < 0) return -1;
    if (needed <= str->capacity) return 0;
    
    size_t new_capacity = str->capacity ? str->capacity : INITIAL_CAPACITY;
    while (new_capacity < needed) new_capacity *= GROW_FACTOR;
    
    char *new_data = realloc(str->data, new_capacity);
    if (!new_data) return -1;
    
    str->data = new_data;
    str->capacity = new_capacity;
    return 0;
}

// Create and destroy
String *string_create(const char *data) {
    if (!data) data = "";
    size_t len = strlen(data);
    String *str = string_create_empty(len + 1);
    if (!str) return NULL;
    if (string_set(str, data) != 0) {
        string_destroy(str);
        return NULL;
    }
    return str;
}

String *string_create_empty(size_t capacity) {
    String *str = malloc(sizeof(String));
    if (!str) return NULL;
    
    str->data = NULL;
    str->length = 0;
    str->capacity = 0;
    
    if (capacity > 0 && string_ensure_capacity(str, capacity) != 0) {
        free(str);
        return NULL;
    }
    
    if (str->capacity > 0) {
        str->data[0] = '\0';
    }
    return str;
}

String *string_create_from(String *other) {
    if (!other) return string_create_empty(0);
    return string_create(string_data(other));
}

void string_destroy(String *str) {
    if (str) {
        free(str->data);
        free(str);
    }
}

// Accessors
const char *string_data(const String *str) {
    return str && str->data ? str->data : "";
}

size_t string_length(const String *str) {
    return str ? str->length : 0;
}

size_t string_capacity(const String *str) {
    return str ? str->capacity : 0;
}

int string_is_empty(const String *str) {
    return !str || str->length == 0;
}

// Modification
int string_append(String *str, const char *data) {
    if (!str || !data) return -1;
    size_t append_len = strlen(data);
    size_t new_len = str->length + append_len + 1;
    
    if (string_ensure_capacity(str, new_len) != 0) return -1;
    
    memcpy(str->data + str->length, data, append_len);
    str->length += append_len;
    str->data[str->length] = '\0';
    return 0;
}

int string_append_string(String *str, const String *other) {
    if (!other) return -1;
    return string_append(str, string_data(other));
}

int string_clear(String *str) {
    if (!str) return -1;
    str->length = 0;
    if (str->data) str->data[0] = '\0';
    return 0;
}

int string_set(String *str, const char *data) {
    if (!str || !data) return -1;
    size_t new_len = strlen(data) + 1;
    
    if (string_ensure_capacity(str, new_len) != 0) return -1;
    
    memcpy(str->data, data, new_len);
    str->length = new_len - 1;
    return 0;
}

int string_resize(String *str, size_t new_capacity) {
    if (!str) return -1;
    if (new_capacity < str->length + 1) new_capacity = str->length + 1;
    
    if (string_ensure_capacity(str, new_capacity) != 0) return -1;
    
    str->capacity = new_capacity;
    str->data[str->length] = '\0';
    return 0;
}

// Operations
String *string_substring(const String *str, size_t start, size_t length) {
    if (!str || start > str->length) return string_create_empty(0);
    
    if (start + length > str->length) length = str->length - start;
    
    String *result = string_create_empty(length + 1);
    if (!result) return NULL;
    
    memcpy(result->data, str->data + start, length);
    result->data[length] = '\0';
    result->length = length;
    return result;
}

int string_compare(const String *str1, const String *str2) {
    if (!str1 || !str2) return str1 == str2 ? 0 : str1 ? 1 : -1;
    return strcmp(str1->data, str2->data);
}

int string_compare_cstr(const String *str, const char *data) {
    if (!str || !data) return str == data ? 0 : str ? 1 : -1;
    return strcmp(str->data, data);
}

int string_find(const String *str, const char *substr, size_t *pos) {
    if (!str || !substr || !pos) return -1;
    char *found = strstr(str->data, substr);
    if (!found) return -1;
    *pos = found - str->data;
    return 0;
}

int string_replace(String *str, const char *find, const char *replace, size_t max_replacements) {
    if (!str || !find || !replace || !*find) return -1;
    
    size_t find_len = strlen(find);
    size_t replace_len = strlen(replace);
    String *result = string_create_empty(str->length + 1);
    if (!result) return -1;
    
    char *current = str->data;
    size_t replacements = 0;
    
    while (*current && (max_replacements == 0 || replacements < max_replacements)) {
        char *next = strstr(current, find);
        if (!next) {
            string_append(result, current);
            break;
        }
        
        // Append up to the match
        size_t prefix_len = next - current;
        char prefix[prefix_len + 1];
        memcpy(prefix, current, prefix_len);
        prefix[prefix_len] = '\0';
        string_append(result, prefix);
        
        // Append replacement
        string_append(result, replace);
        current = next + find_len;
        replacements++;
    }
    
    // Set result back to str
    int ret = string_set(str, string_data(result));
    string_destroy(result);
    return ret;
}

// UTF-8 specific
static int is_utf8_continuation_byte(uint8_t c) {
    return (c & 0xC0) == 0x80;
}

static size_t utf8_char_size(uint8_t c) {
    if ((c & 0x80) == 0) return 1;      // ASCII
    if ((c & 0xE0) == 0xC0) return 2;   // 2-byte
    if ((c & 0xF0) == 0xE0) return 3;   // 3-byte
    if ((c & 0xF8) == 0xF0) return 4;   // 4-byte
    return 0; // Invalid
}

size_t string_utf8_length(const String *str) {
    if (!str || !str->data) return 0;
    
    size_t count = 0;
    for (size_t i = 0; i < str->length;) {
        size_t size = utf8_char_size((uint8_t)str->data[i]);
        if (size == 0 || i + size > str->length) return count; // Invalid or truncated
        i += size;
        count++;
    }
    return count;
}

int string_is_valid_utf8(const String *str) {
    if (!str || !str->data) return 1; // Empty is valid
    
    for (size_t i = 0; i < str->length;) {
        uint8_t c = (uint8_t)str->data[i];
        size_t size = utf8_char_size(c);
        if (size == 0 || i + size > str->length) return 0;
        
        // Check continuation bytes
        for (size_t j = 1; j < size; j++) {
            if (!is_utf8_continuation_byte((uint8_t)str->data[i + j])) return 0;
        }
        
        i += size;
    }
    return 1;
}

int string_utf8_char_at(const String *str, size_t char_index, char *buffer, size_t buffer_size) {
    if (!str || !buffer || buffer_size < 5) return -1; // Need space for max 4-byte char + null
    
    size_t byte_pos = 0;
    size_t current_char = 0;
    
    while (byte_pos < str->length) {
        if (current_char == char_index) {
            size_t size = utf8_char_size((uint8_t)str->data[byte_pos]);
            if (size == 0 || byte_pos + size > str->length || size > buffer_size - 1) return -1;
            
            memcpy(buffer, str->data + byte_pos, size);
            buffer[size] = '\0';
            return 0;
        }
        
        size_t size = utf8_char_size((uint8_t)str->data[byte_pos]);
        if (size == 0 || byte_pos + size > str->length) return -1;
        
        byte_pos += size;
        current_char++;
    }
    
    return -1; // Index out of range
}
