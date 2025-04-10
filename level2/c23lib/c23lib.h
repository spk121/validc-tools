#ifndef C23LIB_H
#define C23LIB_H

#include <stddef.h>
#include <stdarg.h>
#include <time.h>
#include <locale.h>

/**
 * @file c23lib.h
 * @brief Portable C23 library providing POSIX-like string, I/O, time, and locale functions.
 * @details This library extends the C23 standard library with commonly used
 * functions inspired by POSIX and GNU extensions, implemented using only C23
 * standard features. All functions are cross-platform and dependency-free
 * beyond the C23 standard library (<stdio.h>, <stdlib.h>, <string.h>, <ctype.h>,
 * <time.h>, <locale.h>, <stdarg.h>).
 */

/**
 * @typedef ssize_t
 * @brief Signed size type for return values that may indicate errors.
 * @details Defined as a signed counterpart to size_t, typically used to return
 * lengths or -1 on error. Matches POSIX ssize_t but is defined here for C23
 * portability.
 */
// typedef long ssize_t;

/**
 * @brief Duplicates a string by allocating memory and copying it.
 * @param s The null-terminated string to duplicate.
 * @return A pointer to the newly allocated duplicate string, or NULL if
 * allocation fails or s is NULL. The caller must free the returned pointer.
 */
char *c23_strdup(const char *s);

/**
 * @brief Duplicates a string up to a specified length.
 * @param s The null-terminated string to duplicate.
 * @param n Maximum number of characters to copy (excluding null terminator).
 * @return A pointer to the newly allocated string containing up to n characters
 * from s, null-terminated, or NULL if allocation fails or s is NULL. The caller
 * must free the returned pointer.
 */
char *c23_strndup(const char *s, size_t n);

/**
 * @brief Computes the length of a string up to a maximum.
 * @param s The null-terminated string to measure.
 * @param n Maximum number of characters to consider.
 * @return The length of s, up to n, excluding the null terminator. Returns n
 * if no null terminator is found within n characters.
 */
size_t c23_strnlen(const char *s, size_t n);

/**
 * @brief Compares two strings case-insensitively using the current locale.
 * @param s1 The first null-terminated string.
 * @param s2 The second null-terminated string.
 * @return An integer less than, equal to, or greater than zero if s1 is less
 * than, equal to, or greater than s2, respectively, ignoring case. Uses the
 * locale-dependent tolower from <ctype.h>.
 * @note Behavior depends on the system's locale settings.
 */
int c23_strcasecmp(const char *s1, const char *s2);

/**
 * @brief Compares two strings case-insensitively up to a length using the current locale.
 * @param s1 The first null-terminated string.
 * @param s2 The second null-terminated string.
 * @param n Maximum number of characters to compare.
 * @return An integer less than, equal to, or greater than zero if s1 is less
 * than, equal to, or greater than s2, respectively, ignoring case, up to n
 * characters. Uses the locale-dependent tolower from <ctype.h>.
 * @note Behavior depends on the system's locale settings.
 */
int c23_strncasecmp(const char *s1, const char *s2, size_t n);

/**
 * @brief Compares two strings case-insensitively in the C locale (7-bit ASCII).
 * @param s1 The first null-terminated string.
 * @param s2 The second null-terminated string.
 * @return An integer less than, equal to, or greater than zero if s1 is less
 * than, equal to, or greater than s2, respectively, ignoring case. Assumes
 * 7-bit ASCII and uses a locale-independent case conversion.
 * @note Unlike c23_strcasecmp, this is portable and consistent across systems.
 */
int c23_c_strcasecmp(const char *s1, const char *s2);

/**
 * @brief Compares two strings case-insensitively up to a length in the C locale (7-bit ASCII).
 * @param s1 The first null-terminated string.
 * @param s2 The second null-terminated string.
 * @param n Maximum number of characters to compare.
 * @return An integer less than, equal to, or greater than zero if s1 is less
 * than, equal to, or greater than s2, respectively, ignoring case, up to n
 * characters. Assumes 7-bit ASCII and uses a locale-independent case conversion.
 * @note Unlike c23_strncasecmp, this is portable and consistent across systems.
 */
int c23_c_strncasecmp(const char *s1, const char *s2, size_t n);

/**
 * @brief Copies a string and returns a pointer to its end.
 * @param dest Destination buffer (must be large enough).
 * @param src The null-terminated string to copy.
 * @return A pointer to the null terminator of the copied string in dest.
 * @note The caller must ensure dest has sufficient space.
 */
char *c23_stpcpy(char *dest, const char *src);

/**
 * @brief Copies a string up to a length and returns a pointer to its end.
 * @param dest Destination buffer (must be at least n bytes).
 * @param src The null-terminated string to copy.
 * @param n Maximum number of characters to copy (excluding null terminator).
 * @return A pointer to the end of the copied string in dest (position after
 * the last copied character, which may not be null-terminated if n is reached).
 * @note The caller must ensure dest has sufficient space.
 */
char *c23_stpncpy(char *dest, const char *src, size_t n);

/**
 * @brief Tokenizes a string by splitting on delimiters, handling empty fields.
 * @param stringp Pointer to the string to tokenize; updated to point past the
 * token on return.
 * @param delim Null-terminated string of delimiter characters.
 * @return A pointer to the next token (null-terminated), or NULL if no more
 * tokens exist or stringp is NULL/invalid. Empty tokens are returned as "".
 * @note Unlike strtok, this handles consecutive delimiters and is not
 * thread-static.
 */
char *c23_strsep(char **stringp, const char *delim);

/**
 * @brief Reads a line from a stream into a dynamically allocated buffer.
 * @param lineptr Pointer to a buffer pointer; if NULL or insufficient, it’s allocated/reallocated.
 * @param n Pointer to the buffer size; updated if reallocated.
 * @param stream The input stream to read from.
 * @return Number of characters read (including newline, if present), or -1 on error or EOF with no data.
 * @note The caller must free *lineptr if it’s allocated by this function.
 */
ssize_t c23_getline(char **lineptr, size_t *n, FILE *stream);

/**
 * @brief Reads from a stream into a dynamically allocated buffer until a delimiter.
 * @param lineptr Pointer to a buffer pointer; if NULL or insufficient, it’s allocated/reallocated.
 * @param n Pointer to the buffer size; updated if reallocated.
 * @param delim The delimiter character to stop at.
 * @param stream The input stream to read from.
 * @return Number of characters read (including delimiter, if present), or -1 on error or EOF with no data.
 * @note The caller must free *lineptr if it’s allocated by this function.
 */
ssize_t c23_getdelim(char **lineptr, size_t *n, int delim, FILE *stream);

/**
 * @brief Formats a string and allocates memory for it.
 * @param strp Pointer to where the allocated string will be stored.
 * @param fmt Format string (printf-style).
 * @param ... Arguments for the format string.
 * @return Number of characters that would be written (excluding null), or -1 on error.
 * @note The caller must free *strp on success.
 */
int c23_asprintf(char **strp, const char *fmt, ...);

/**
 * @brief Formats a string with va_list and allocates memory for it.
 * @param strp Pointer to where the allocated string will be stored.
 * @param fmt Format string (printf-style).
 * @param args Variable argument list.
 * @return Number of characters that would be written (excluding null), or -1 on error.
 * @note The caller must free *strp on success.
 */
int c23_vasprintf(char **strp, const char *fmt, va_list args);

/**
 * @brief Parses a string into a struct tm according to a format.
 * @param s The null-terminated string to parse.
 * @param format Format string (e.g., "%Y-%m-%d %H:%M:%S").
 * @param tm Pointer to struct tm to fill.
 * @return Pointer to the first unparsed character in s, or NULL on error.
 * @note Supports basic formats: %Y (year), %m (month), %d (day), %H (hour),
 * %M (minute), %S (second). Other formats return NULL.
 */
char *c23_strptime(const char *s, const char *format, struct tm *tm);

/**
 * @brief Converts a UTC struct tm to a time_t.
 * @param tm Pointer to struct tm representing UTC time.
 * @return The time_t value in UTC, or -1 on error.
 * @note Approximates UTC by adjusting for local time offset; may be imprecise
 * with DST or edge cases.
 */
time_t c23_timegm(struct tm *tm);

/**
 * @brief Converts a time_t to a string, reentrant version.
 * @param timep Pointer to time_t value.
 * @param buf Buffer to store the string (at least 26 bytes).
 * @return Pointer to buf, or NULL on error.
 * @note Format is "Day Mon DD HH:MM:SS YYYY\n" (26 chars including null).
 */
char *c23_ctime_r(const time_t *timep, char *buf);

/**
 * @brief Converts a struct tm to a string, reentrant version.
 * @param tm Pointer to struct tm.
 * @param buf Buffer to store the string (at least 26 bytes).
 * @return Pointer to buf, or NULL on error.
 * @note Format is "Day Mon DD HH:MM:SS YYYY\n" (26 chars including null).
 */
char *c23_asctime_r(const struct tm *tm, char *buf);

/**
 * @brief Formats a monetary value according to the current locale.
 * @param s Buffer to store the formatted string.
 * @param maxsize Maximum size of the buffer (including null terminator).
 * @param format Format string (e.g., "%$%f" for currency symbol and value).
 * @param val The monetary value to format.
 * @return Number of bytes written to s (excluding null), or -1 on error.
 * @note Supports basic formats: %$ (currency symbol), %f (value with 2 decimals).
 * Uses localeconv() for decimal point and currency symbol, but lacks full POSIX
 * strfmon features (e.g., grouping, international symbols).
 */
ssize_t c23_strfmon(char *s, size_t maxsize, const char *format, double val);

#endif
