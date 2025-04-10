#include "c23lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Private C-locale tolower for 7-bit ASCII
static int c23_c_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

char *c23_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) strcpy(dup, s);
    return dup;
}

char *c23_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = c23_strnlen(s, n);
    char *dup = malloc(len + 1);
    if (dup) {
        strncpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}

size_t c23_strnlen(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && s[i]) i++;
    return i;
}

int c23_strcasecmp(const char *s1, const char *s2) {
    while (*s1 && (tolower((unsigned char)*s1) == tolower((unsigned char)*s2))) {
        s1++; s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int c23_strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n-- && *s1 && (tolower((unsigned char)*s1) == tolower((unsigned char)*s2))) {
        s1++; s2++;
    }
    return n == (size_t)-1 ? 0 : tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int c23_c_strcasecmp(const char *s1, const char *s2) {
    while (*s1 && (c23_c_tolower((unsigned char)*s1) == c23_c_tolower((unsigned char)*s2))) {
        s1++; s2++;
    }
    return c23_c_tolower((unsigned char)*s1) - c23_c_tolower((unsigned char)*s2);
}

int c23_c_strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n-- && *s1 && (c23_c_tolower((unsigned char)*s1) == c23_c_tolower((unsigned char)*s2))) {
        s1++; s2++;
    }
    return n == (size_t)-1 ? 0 : c23_c_tolower((unsigned char)*s1) - c23_c_tolower((unsigned char)*s2);
}

char *c23_stpcpy(char *dest, const char *src) {
    strcpy(dest, src);
    return dest + strlen(src);
}

char *c23_stpncpy(char *dest, const char *src, size_t n) {
    size_t len = c23_strnlen(src, n);
    strncpy(dest, src, n);
    return dest + len;
}

char *c23_strsep(char **stringp, const char *delim) {
    if (!stringp || !*stringp) return NULL;
    char *start = *stringp;
    char *p = start;
    while (*p && !strchr(delim, *p)) p++;
    if (*p) {
        *p = '\0';
        *stringp = p + 1;
    } else {
        *stringp = NULL;
    }
    return start;
}

ssize_t c23_getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;
    if (!*lineptr) {
        *n = 128; // Initial size
        *lineptr = malloc(*n);
        if (!*lineptr) return -1;
    }
    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            *n *= 2;
            char *tmp = realloc(*lineptr, *n);
            if (!tmp) { free(*lineptr); *lineptr = NULL; return -1; }
            *lineptr = tmp;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == '\n') break;
    }
    if (pos == 0 && c == EOF) return -1;
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

ssize_t c23_getdelim(char **lineptr, size_t *n, int delim, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;
    if (!*lineptr) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) return -1;
    }
    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            *n *= 2;
            char *tmp = realloc(*lineptr, *n);
            if (!tmp) { free(*lineptr); *lineptr = NULL; return -1; }
            *lineptr = tmp;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == delim) break;
    }
    if (pos == 0 && c == EOF) return -1;
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

int c23_asprintf(char **strp, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = c23_vasprintf(strp, fmt, args);
    va_end(args);
    return ret;
}

int c23_vasprintf(char **strp, const char *fmt, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);
    int size = vsnprintf(NULL, 0, fmt, args_copy); // Get required size
    va_end(args_copy);
    if (size < 0) return -1;
    *strp = malloc(size + 1);
    if (!*strp) return -1;
    return vsnprintf(*strp, size + 1, fmt, args);
}

char *c23_strptime(const char *s, const char *format, struct tm *tm) {
    char *p = (char *)s;
    int n;
    while (*format) {
        if (*format != '%') {
            if (*p++ != *format++) return NULL;
            continue;
        }
        format++;
        switch (*format++) {
            case 'Y': if (sscanf(p, "%4d%n", &tm->tm_year, &n) != 1) return NULL; tm->tm_year -= 1900; p += n; break;
            case 'm': if (sscanf(p, "%2d%n", &tm->tm_mon, &n) != 1) return NULL; tm->tm_mon--; p += n; break;
            case 'd': if (sscanf(p, "%2d%n", &tm->tm_mday, &n) != 1) return NULL; p += n; break;
            case 'H': if (sscanf(p, "%2d%n", &tm->tm_hour, &n) != 1) return NULL; p += n; break;
            case 'M': if (sscanf(p, "%2d%n", &tm->tm_min, &n) != 1) return NULL; p += n; break;
            case 'S': if (sscanf(p, "%2d%n", &tm->tm_sec, &n) != 1) return NULL; p += n; break;
            default: return NULL; // Unsupported format
        }
    }
    return p;
}

time_t c23_timegm(struct tm *tm) {
    time_t t = mktime(tm); // Local time
    struct tm *local = localtime(&t);
    if (!local) return -1;
    int offset = (local->tm_hour - tm->tm_hour) * 3600 + 
                 (local->tm_min - tm->tm_min) * 60 + 
                 (local->tm_sec - tm->tm_sec);
    return t + offset;
}

char *c23_ctime_r(const time_t *timep, char *buf) {
    struct tm *tm = localtime(timep);
    if (!tm) return NULL;
    strftime(buf, 26, "%a %b %d %H:%M:%S %Y\n", tm);
    return buf;
}

char *c23_asctime_r(const struct tm *tm, char *buf) {
    strftime(buf, 26, "%a %b %d %H:%M:%S %Y\n", tm);
    return buf;
}

ssize_t c23_strfmon(char *s, size_t maxsize, const char *format, double val) {
    struct lconv *lc = localeconv();
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%.2f", val); // Basic number
    if (len < 0) return -1;

    char *out = s;
    size_t pos = 0;
    while (*format && pos < maxsize - 1) {
        if (*format != '%') {
            out[pos++] = *format++;
            continue;
        }
        format++; // Skip '%'
        if (*format == '$') {
            const char *sym = lc->currency_symbol;
            while (*sym && pos < maxsize - 1) out[pos++] = *sym++;
            format++;
        } else if (*format == 'f') {
            char *p = buf;
            while (*p && pos < maxsize - 1) out[pos++] = *p++;
            format++;
        } else {
            return -1; // Unsupported
        }
    }
    out[pos] = '\0';
    return (ssize_t)pos;
}

void x(void)
{
    size_t j;
    signed size_t k = -100;
}