#ifndef ZSTRING_H
#define ZSTRING_H

/**
 * @file zstring.h
 * @brief Typedefs for null-terminated C strings
 *
 * Provides typedefs for null-terminated strings (zstrings) to improve code clarity
 * and intent, inspired by the C++ Core Guidelines.
 */

/**
 * @typedef zstring
 * @brief A pointer to a null-terminated mutable C string
 *
 * Represents a char array terminated by '\0'. The string is mutable,
 * allowing modification of its contents.
 */
typedef char* zstring;

/**
 * @typedef czstring
 * @brief A pointer to a null-terminated constant C string
 *
 * Represents a char array terminated by '\0'. The string is immutable,
 * preventing modification of its contents.
 */
typedef const char* czstring;

#endif // ZSTRING_H
