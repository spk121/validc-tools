
#include <stdarg.h>
#include "logging.h"

// Define the global threshold
LogLevel g_log_threshold = LOG_ERROR;  // Default to ERROR level

// Internal logging function
static void log_message(LogLevel level, const char* level_str, const char* format, va_list args) {
    if (level < g_log_threshold) {
        return;
    }

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    
    fprintf(stderr, "[%s] %s\n", level_str, buffer);
    fflush(stderr);
}

// Initialization function checking environment variable
void logging_init(void) {
    const char* env_level = getenv("LOG_LEVEL");
    
    if (env_level != NULL) {
        if (strcasecmp(env_level, "DEBUG") == 0) {
            g_log_threshold = LOG_DEBUG;
        } else if (strcasecmp(env_level, "WARN") == 0) {
            g_log_threshold = LOG_WARN;
        } else if (strcasecmp(env_level, "ERROR") == 0) {
            g_log_threshold = LOG_ERROR;
        } else if (strcasecmp(env_level, "NONE") == 0) {
            g_log_threshold = LOG_NONE;
        }
    }
}

// Public logging functions
void log_debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_DEBUG, "DEBUG", format, args);
    va_end(args);
}

void log_warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_WARN, "WARN", format, args);
    va_end(args);
}

void log_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_ERROR, "ERROR", format, args);
    va_end(args);
}

// Updated example usage with different types
#ifdef EXAMPLE_USAGE
int test_function(float fvalue, double dvalue, int ivalue) {
    char c1 = 'a', c2 = 'a';
    short s1 = 5, s2 = 10;
    long l1 = 100L, l2 = 50L;

    return_if_eq(c1, c2);         // char comparison
    return_val_if_lt(s1, s2, -1); // short comparison
    return_if_gt(fvalue, 0.0f);   // float comparison
    return_val_if_le(dvalue, 1.0, 42); // double comparison
    return_if_ge(l1, l2);         // long comparison
    return_val_if_eq(ivalue, 5, 99); // int comparison
    
    return 0;
}

int main(void) {
    logging_init();
    
    test_function(1.5f, 0.5, 5);
    
    return 0;
}
#endif
