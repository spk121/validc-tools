#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "bre.h"

// Forward declarations
static bool match_here(const char *text, const char *pattern, BreMatch *match, int *group_count);
static bool match_star(const char *text, const char *pattern, char c, BreMatch *match, int group_count);

bool bre_match(const char *text, const char *pattern, BreMatch *match) {
    if (!text || !pattern || !match) return false;

    match->start = -1;
    match->length = 0;
    match->num_groups = 0;
    for (int i = 0; i < BRE_MAX_GROUPS; i++) {
        match->groups[i].start = -1;
        match->groups[i].length = 0;
    }

    if (*pattern == '^') {
        if (match_here(text, pattern + 1, match, &match->num_groups)) {
            match->start = 0;
            match->length = strlen(text);
            return true;
        }
        return false;
    }

    // Try matching at every position
    for (const char *t = text; ; t++) {
        if (match_here(t, pattern, match, &match->num_groups)) {
            match->start = (int)(t - text);
            match->length = strlen(t);
            return true;
        }
        if (!*t) break;
    }
    return false;
}

static bool match_here(const char *text, const char *pattern, BreMatch *match, int *group_count) {
    if (*pattern == '\0') return true;
    if (*pattern == '$' && *(pattern + 1) == '\0') return *text == '\0';

    if (*(pattern + 1) == '*') {
        return match_star(text, pattern + 2, *pattern, match, *group_count);
    }

    if (*text == '\0') return false;

    if (*pattern == '.') {
        return match_here(text + 1, pattern + 1, match, group_count);
    }

    if (*pattern == '[') {
        bool invert = *(pattern + 1) == '^';
        const char *p = pattern + (invert ? 2 : 1);
        bool matched = false;
        char last = '\0';

        while (*p && *p != ']') {
            if (*p == '-' && last && *(p + 1) != ']') {
                char start = last;
                char end = *(p + 1);
                if (*text >= start && *text <= end) matched = true;
                p += 2;
            } else {
                if (*p == *text) matched = true;
                last = *p;
                p++;
            }
        }
        if (!*p) return false; // Unclosed bracket
        if (invert) matched = !matched;
        return matched && match_here(text + 1, p + 1, match, group_count);
    }

    if (*pattern == '\\' && *(pattern + 1) == '(') {
        if (*group_count >= BRE_MAX_GROUPS) return false; // Too many groups
        int group_idx = (*group_count)++;
        match->groups[group_idx].start = (int)(text - (match->start >= 0 ? text - match->start : text));
        
        const char *p = pattern + 2;
        int depth = 1;
        const char *group_start = text;

        while (*p && depth > 0) {
            if (*p == '\\' && *(p + 1) == '(') depth++;
            if (*p == '\\' && *(p + 1) == ')') depth--;
            if (depth > 0) p++;
            if (*text && match_here(text, p - (depth > 0 ? 1 : 0), match, group_count)) {
                text++;
            } else if (depth > 0) {
                return false;
            }
        }
        if (depth > 0) return false; // Unmatched parenthesis

        match->groups[group_idx].length = (int)(text - group_start);
        return match_here(text, p + 1, match, group_count);
    }

    return *text == *pattern && match_here(text + 1, pattern + 1, match, group_count);
}

static bool match_star(const char *text, const char *pattern, char c, BreMatch *match, int group_count) {
    do {
        if (match_here(text, pattern, match, &group_count)) return true;
    } while (*text && (c == '.' || *text == c) && text++);
    return false;
}

char *bre_substitute(const char *text, const char *pattern, const char *replacement) {
    if (!text || !pattern || !replacement) return NULL;

    BreMatch match;
    if (!bre_match(text, pattern, &match) || match.start < 0) {
        return strdup(text); // No match, return copy
    }

    // Calculate new length
    size_t prefix_len = match.start;
    size_t suffix_len = strlen(text) - match.start - match.length;
    size_t repl_len = 0;

    // Compute replacement length with backreferences
    for (const char *r = replacement; *r; r++) {
        if (*r == '\\' && *(r + 1) >= '1' && *(r + 1) <= '9') {
            int group = *(r + 1) - '1';
            if (group < match.num_groups && match.groups[group].start >= 0) {
                repl_len += match.groups[group].length;
            }
            r++; // Skip digit
        } else {
            repl_len++;
        }
    }

    size_t new_len = prefix_len + repl_len + suffix_len;
    char *result = malloc(new_len + 1);
    if (!result) return NULL;

    // Build result
    memcpy(result, text, prefix_len);
    char *p = result + prefix_len;

    for (const char *r = replacement; *r; r++) {
        if (*r == '\\' && *(r + 1) >= '1' && *(r + 1) <= '9') {
            int group = *(r + 1) - '1';
            if (group < match.num_groups && match.groups[group].start >= 0) {
                memcpy(p, text + match.groups[group].start, match.groups[group].length);
                p += match.groups[group].length;
            }
            r++;
        } else {
            *p++ = *r;
        }
    }

    memcpy(p, text + match.start + match.length, suffix_len);
    result[new_len] = '\0';

    return result;
}
