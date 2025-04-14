#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zstring.h" // Include zstring definitions

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
    LOG_FATAL = 3, /**< Fatal level - unrecoverable errors, aborts program */
    LOG_NONE  = 4  /**< No logging - disable all output */
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
 * Valid values are "DEBUG", "WARN", "ERROR", "FATAL", or "NONE".
 * Also reads LOG_ABORT_LEVEL to determine if WARN or ERROR should abort.
 * Should be called once at program startup.
 */
void logging_init(void);

/**
 * @brief Log a debug message
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_debug(czstring format, ...);

/**
 * @brief Log a warning message
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_warn(czstring format, ...);

/**
 * @brief Log an error message
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_error(czstring format, ...);

/**
 * @brief Log a fatal message and abort
 * @param format Format string (printf-style)
 * @param ... Variable arguments for the format string
 */
void log_fatal(czstring format, ...);

// Existing recoverable precondition helpers

#define return_if_null(ptr) do { \
    if ((ptr) == NULL) { \
        log_error("Precondition failed at %s:%d - %s is NULL", __func__, __LINE__, #ptr); \
        return; \
    } \
} while(0)

#define return_val_if_null(ptr, val) do { \
    if ((ptr) == NULL) { \
        log_error("Precondition failed at %s:%d - %s is NULL", __func__, __LINE__, #ptr); \
        return (val); \
    } \
} while(0)

#define return_if(condition) do { \
    if (condition) { \
        log_error("Precondition failed at %s:%d - %s", __func__, __LINE__, #condition); \
        return; \
    } \
} while(0)

#define return_val_if(condition, val) do { \
    if (condition) { \
        log_error("Precondition failed at %s:%d - %s", __func__, __LINE__, #condition); \
        return (val); \
    } \
} while(0)

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

// Non-recoverable precondition macros using log_fatal

#define Expects_not_null(ptr) do { \
    if ((ptr) == NULL) { \
        log_fatal("Contract violation at %s:%d - %s is NULL", __func__, __LINE__, #ptr); \
    } \
} while(0)

#define Expects(condition) do { \
    if (!(condition)) { \
        log_fatal("Contract violation at %s:%d - %s", __func__, __LINE__, #condition); \
    } \
} while(0)

#define Expects_eq(a, b) do { \
    if ((a) != (b)) { \
        _Generic((a), \
            char: log_fatal("Contract violation at %s:%d - %s != %s (%c != %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_fatal("Contract violation at %s:%d - %s != %s (%hd != %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_fatal("Contract violation at %s:%d - %s != %s (%d != %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_fatal("Contract violation at %s:%d - %s != %s (%ld != %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_fatal("Contract violation at %s:%d - %s != %s (%f != %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_fatal("Contract violation at %s:%d - %s != %s (%lf != %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_fatal("Contract violation at %s:%d - %s != %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
    } \
} while(0)

#define Expects_ne(a, b) do { \
    if ((a) == (b)) { \
        _Generic((a), \
            char: log_fatal("Contract violation at %s:%d - %s == %s (%c == %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_fatal("Contract violation at %s:%d - %s == %s (%hd == %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_fatal("Contract violation at %s:%d - %s == %s (%d == %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_fatal("Contract violation at %s:%d - %s == %s (%ld == %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_fatal("Contract violation at %s:%d - %s == %s (%f == %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_fatal("Contract violation at %s:%d - %s == %s (%lf == %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_fatal("Contract violation at %s:%d - %s == %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
    } \
} while(0)

#define Expects_lt(a, b) do { \
    if (!((a) < (b))) { \
        _Generic((a), \
            char: log_fatal("Contract violation at %s:%d - %s >= %s (%c >= %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_fatal("Contract violation at %s:%d - %s >= %s (%hd >= %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_fatal("Contract violation at %s:%d - %s >= %s (%d >= %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_fatal("Contract violation at %s:%d - %s >= %s (%ld >= %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_fatal("Contract violation at %s:%d - %s >= %s (%f >= %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_fatal("Contract violation at %s:%d - %s >= %s (%lf >= %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_fatal("Contract violation at %s:%d - %s >= %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
    } \
} while(0)

#define Expects_gt(a, b) do { \
    if (!((a) > (b))) { \
        _Generic((a), \
            char: log_fatal("Contract violation at %s:%d - %s <= %s (%c <= %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_fatal("Contract violation at %s:%d - %s <= %s (%hd <= %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_fatal("Contract violation at %s:%d - %s <= %s (%d <= %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_fatal("Contract violation at %s:%d - %s <= %s (%ld <= %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_fatal("Contract violation at %s:%d - %s <= %s (%f <= %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_fatal("Contract violation at %s:%d - %s <= %s (%lf <= %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_fatal("Contract violation at %s:%d - %s <= %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
    } \
} while(0)

#define Expects_le(a, b) do { \
    if (!((a) <= (b))) { \
        _Generic((a), \
            char: log_fatal("Contract violation at %s:%d - %s > %s (%c > %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_fatal("Contract violation at %s:%d - %s > %s (%hd > %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_fatal("Contract violation at %s:%d - %s > %s (%d > %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_fatal("Contract violation at %s:%d - %s > %s (%ld > %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_fatal("Contract violation at %s:%d - %s > %s (%f > %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_fatal("Contract violation at %s:%d - %s > %s (%lf > %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_fatal("Contract violation at %s:%d - %s > %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
    } \
} while(0)

#define Expects_ge(a, b) do { \
    if (!((a) >= (b))) { \
        _Generic((a), \
            char: log_fatal("Contract violation at %s:%d - %s < %s (%c < %c)", __func__, __LINE__, #a, #b, (a), (b)), \
            short: log_fatal("Contract violation at %s:%d - %s < %s (%hd < %hd)", __func__, __LINE__, #a, #b, (a), (b)), \
            int: log_fatal("Contract violation at %s:%d - %s < %s (%d < %d)", __func__, __LINE__, #a, #b, (a), (b)), \
            long: log_fatal("Contract violation at %s:%d - %s < %s (%ld < %ld)", __func__, __LINE__, #a, #b, (a), (b)), \
            float: log_fatal("Contract violation at %s:%d - %s < %s (%f < %f)", __func__, __LINE__, #a, #b, (a), (b)), \
            double: log_fatal("Contract violation at %s:%d - %s < %s (%lf < %lf)", __func__, __LINE__, #a, #b, (a), (b)), \
            default: log_fatal("Contract violation at %s:%d - %s < %s (unknown type)", __func__, __LINE__, #a, #b) \
        ); \
    } \
} while(0)

#endif // LOGGING_H
