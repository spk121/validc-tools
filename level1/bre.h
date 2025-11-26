#ifndef BRE_H
#define BRE_H

#include <stdbool.h>

#define BRE_MAX_GROUPS 9 // Maximum number of capture groups (1-9)

/* Structure to hold match result and capture groups */
typedef struct {
    int start;              // Start index of full match (-1 if no match)
    int length;             // Length of full match
    struct {
        int start;          // Start index of capture group
        int length;         // Length of capture group
    } groups[BRE_MAX_GROUPS];
    int num_groups;         // Number of groups captured
} BreMatch;

/* Match a BRE pattern against a string.
 * Fills 'match' with the match position and capture groups.
 * Returns true if a match is found, false otherwise.
 */
bool bre_match(const char *text, const char *pattern, BreMatch *match);

/* Substitute the first match of a BRE pattern with a replacement.
 * Returns a newly allocated string with the substitution, or NULL on error.
 * Caller must free the result.
 * 'text' is the input, 'pattern' is the BRE pattern, 'replacement' includes \1-\9.
 */
char *bre_substitute(const char *text, const char *pattern, const char *replacement);

#endif /* BRE_H */
