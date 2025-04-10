#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @file logging.h
 * @brief Logging and precondition checking utilities
 *
 * This header provides a logging system with multiple levels and precondition
 * checking macros that log failures with detailed information.
 */

/**
 * @enum LogLevel
 * @brief Enumerates available logging levels
 */
typedef enum {
    LOG_DEBUG = 0, /**< Debug level - detailed diagnostic information */
    LOG_WARN  = 1, /**< Warning level - potential issues */
    LOG_ERROR = 2, /**< Error level - error conditions */
    LOG_NONE  = 3  /**< No logging - disable all output */
} LogLevel;

/**
 * @var g_log_threshold
 * @brief Global logging threshold
 *
 * Controls which log messages are output based on their level.
 * Messages below this threshold are suppressed.
 */
extern LogLevel g_log_threshold;

/**
 * @brief Initialize the logging system
 *
 * Reads the LOG_LEVEL environment variable to set the logging threshold.
 * Valid values are "DEBUG", "WARN", "ERROR", or "NONE".
 * Should be called once at program startup.
 */
void logging_init(void);

/**
 * @brief Log a debug message
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_debug(const char* format, ...);

/**
 * @brief Log a warning message
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_warn(const char* format, ...);

/**
 * @brief Log an error message
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_error(const char* format, ...);

// Basic precondition helpers

/**
 * @brief Return from function if pointer is NULL
 * @param ptr Pointer to check
 * @note Logs an error with function name, line number, and pointer name if NULL
 */
#define return_if_null(ptr) do { \
    if ((ptr) == NULL) { \
        log_error("Precondition failed at %s:%d - %s is NULL", __func__, __LINE__, #ptr); \
        return; \
    } \
} while(0)

/**
 * @brief Return specified value if pointer is NULL
 * @param ptr Pointer to check
 * @param val Value to return if pointer is NULL
 * @note Logs an error with function name, line number, and pointer name if NULL
 */
#define return_val_if_null(ptr, val) do { \
    if ((ptr) == NULL) { \
        log_error("Precondition failed at %s:%d - %s is NULL", __func__, __LINE__, #ptr); \
        return (val); \
    } \
} while(0)

/**
 * @brief Return from function if condition is true
 * @param condition Boolean expression to evaluate
 * @note Logs an error with function name, line number, and condition text if true
 */
#define return_if(condition) do { \
    if (condition) { \
        log_error("Precondition failed at %s:%d - %s", __func__, __LINE__, #condition); \
        return; \
    } \
} while(0)

/**
 * @brief Return specified value if condition is true
 * @param condition Boolean expression to evaluate
 * @param val Value to return if condition is true
 * @note Logs an error with function name, line number, and condition text if true
 */
#define return_val_if(condition, val) do { \
    if (condition) { \
        log_error("Precondition failed at %s:%d - %s", __func__, __LINE__, #condition); \
        return (val); \
    } \
} while(0)

// Comparison precondition helpers with type-generic logging

/**
 * @brief Return if two values are equal
 * @param a First value to compare
 * @param b Second value to compare
 * @note Logs an error with function name, line number, expressions, and values if equal
 * @note Supports char, short, int, long, float, and double types
 */
#define return_if_eq(a, b) do { \
    if ((a) == (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s == %s (%c == %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s == %s (%hd == %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s == %s (%d == %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s == %s (%ld == %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s == %s (%f == %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s == %s (%lf == %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s == %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return; \
    } \
} while(0)

/**
 * @brief Return specified value if two values are equal
 * @param a First value to compare
 * @param b Second value to compare
 * @param val Value to return if equal
 * @note Logs an error with function name, line number, expressions, and values if equal
 * @note Supports char, short, int, long, float, and double types
 */
#define return_val_if_eq(a, b, val) do { \
    if ((a) == (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s == %s (%c == %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s == %s (%hd == %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s == %s (%d == %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s == %s (%ld == %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s == %s (%f == %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s == %s (%lf == %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s == %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return (val); \
    } \
} while(0)

/**
 * @brief Return if first value is less than second
 * @param a First value to compare
 * @param b Second value to compare
 * @note Logs an error with function name, line number, expressions, and values if true
 * @note Supports char, short, int, long, float, and double types
 */
#define return_if_lt(a, b) do { \
    if ((a) < (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s < %s (%c < %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s < %s (%hd < %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s < %s (%d < %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s < %s (%ld < %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s < %s (%f < %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s < %s (%lf < %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s < %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return; \
    } \
} while(0)

/**
 * @brief Return specified value if first value is less than second
 * @param a First value to compare
 * @param b Second value to compare
 * @param val Value to return if less than
 * @note Logs an error with function name, line number, expressions, and values if true
 * @note Supports char, short, int, long, float, and double types
 */
#define return_val_if_lt(a, b, val) do { \
    if ((a) < (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s < %s (%c < %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s < %s (%hd < %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s < %s (%d < %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s < %s (%ld < %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s < %s (%f < %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s < %s (%lf < %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s < %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return (val); \
    } \
} while(0)

/**
 * @brief Return if first value is greater than second
 * @param a First value to compare
 * @param b Second value to compare
 * @note Logs an error with function name, line number, expressions, and values if true
 * @note Supports char, short, int, long, float, and double types
 */
#define return_if_gt(a, b) do { \
    if ((a) > (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s > %s (%c > %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s > %s (%hd > %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s > %s (%d > %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s > %s (%ld > %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s > %s (%f > %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s > %s (%lf > %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s > %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return; \
    } \
} while(0)

/**
 * @brief Return specified value if first value is greater than second
 * @param a First value to compare
 * @param b Second value to compare
 * @param val Value to return if greater than
 * @note Logs an error with function name, line number, expressions, and values if true
 * @note Supports char, short, int, long, float, and double types
 */
#define return_val_if_gt(a, b, val) do { \
    if ((a) > (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s > %s (%c > %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s > %s (%hd > %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s > %s (%d > %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s > %s (%ld > %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s > %s (%f > %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s > %s (%lf > %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s > %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return (val); \
    } \
} while(0)

/**
 * @brief Return if first value is less than or equal to second
 * @param a First value to compare
 * @param b Second value to compare
 * @note Logs an error with function name, line number, expressions, and values if true
 * @note Supports char, short, int, long, float, and double types
 */
#define return_if_le(a, b) do { \
    if ((a) <= (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s <= %s (%c <= %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s <= %s (%hd <= %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s <= %s (%d <= %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s <= %s (%ld <= %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s <= %s (%f <= %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s <= %s (%lf <= %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s <= %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return; \
    } \
} while(0)

/**
 * @brief Return specified value if first value is less than or equal to second
 * @param a First value to compare
 * @param b Second value to compare
 * @param val Value to return if less than or equal
 * @note Logs an error with function name, line number, expressions, and values if true
 * @note Supports char, short, int, long, float, and double types
 */
#define return_val_if_le(a, b, val) do { \
    if ((a) <= (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s <= %s (%c <= %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s <= %s (%hd <= %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s <= %s (%d <= %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s <= %s (%ld <= %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s <= %s (%f <= %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s <= %s (%lf <= %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s <= %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return (val); \
    } \
} while(0)

/**
 * @brief Return if first value is greater than or equal to second
 * @param a First value to compare
 * @param b Second value to compare
 * @note Logs an error with function name, line number, expressions, and values if true
 * @note Supports char, short, int, long, float, and double types
 */
#define return_if_ge(a, b) do { \
    if ((a) >= (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s >= %s (%c >= %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s >= %s (%hd >= %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s >= %s (%d >= %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s >= %s (%ld >= %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s >= %s (%f >= %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s >= %s (%lf >= %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s >= %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return; \
    } \
} while(0)

/**
 * @brief Return specified value if first value is greater than or equal to second
 * @param a First value to compare
 * @param b Second value to compare
 * @param val Value to return if greater than or equal
 * @note Logs an error with function name, line number, expressions, and values if true
 * @note Supports char, short, int, long, float, and double types
 */
#define return_val_if_ge(a, b, val) do { \
    if ((a) >= (b)) { \
        _Generic((a), \
            char: log_error("Precondition failed at %s:%d - %s >= %s (%c >= %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_error("Precondition failed at %s:%d - %s >= %s (%hd >= %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_error("Precondition failed at %s:%d - %s >= %s (%d >= %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_error("Precondition failed at %s:%d - %s >= %s (%ld >= %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_error("Precondition failed at %s:%d - %s >= %s (%f >= %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_error("Precondition failed at %s:%d - %s >= %s (%lf >= %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_error("Precondition failed at %s:%d - %s >= %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
        return (val); \
    } \
} while(0)

#endif // LOGGING_H
