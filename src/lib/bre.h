#ifndef BRE_H
#define BRE_H

#include <stdbool.h>

#define BRE_MAX_GROUPS 9 // Maximum number of capture groups (1-9)

/* Success/No-match/Error result code for engine functions */
typedef enum
{
    BRE_OK = 0,      // Operation/match succeeded; out params contain valid data
    BRE_NOMATCH = 1, // No match (generic), or not applicable for non-group atoms
    BRE_ERROR = 2,   // Malformed pattern or unrecoverable error

    // Group-specific disambiguation:
    BRE_NOT_GROUP = 3,     // Pattern at current position is not a capture group
    BRE_GROUP_MISMATCH = 4 // Pattern is a capture group but it failed to match at current text position
} BreResult;

/* Structure to hold match result and capture groups */
typedef struct
{
    int start;  // Start index of full match (-1 if no match)
    int length; // Length of full match
    struct
    {
        int start;  // Start index of capture group
        int length; // Length of capture group
    } groups[BRE_MAX_GROUPS];
    int num_groups; // Number of groups captured
} BreMatch;

typedef struct
{
    const char *base; // base pointer to text (for absolute positions)
    const char *text; // text being matched
    int ti;           // text index (current position in text)
    const char *pat;  // pattern string
    int pi;           // pattern index (current position in pattern)
    int pend;         // pattern end (length of pattern)
} MatchContext;

typedef struct
{
    int min;     // minimum repetitions
    int max;     // maximum repetitions (-1 for unbounded)
    int next_pi; // next pattern index after the repetition spec
} BreRepetition;

/* Match a BRE pattern against a string.
 * Fills 'match' with the match position and capture groups.
 * Returns BRE_OK if a match is found, BRE_NOMATCH if none, BRE_ERROR on syntax errors.
 */
BreResult bre_match(const char *text, const char *pattern, BreMatch *match);

/* Substitute the first match of a BRE pattern with a replacement.
 * Returns a newly allocated string with the substitution, or NULL on error.
 * Caller must free the result.
 * 'text' is the input; 'pattern' is the BRE pattern; 'replacement' includes \1-\9.
 * If there is no match, returns a copy of 'text'.
 */
char *bre_substitute(const char *text, const char *pattern, const char *replacement);

/* Exposed for tests: BRE repetition parser \{n\}, \{n,\}, \{n,m\}
 * Returns:
 *   BRE_OK     if a valid repetition was parsed and out params are set
 *   BRE_NOMATCH if there's no repetition at pi
 *   BRE_ERROR  for malformed sequences
 */
BreResult parse_bre_repetition(const char *pat, int pi, int pend, BreRepetition *rep);

BreResult match_group_without_quantifier(MatchContext *ctx, BreMatch *m, int inner_start, int inner_end, int atom_end,
                                         int *total_out);
BreResult match_group_with_quantifier(MatchContext *ctx, BreMatch *m, int inner_start, int inner_end, int atom_start,
                                      int atom_end, const BreRepetition *rep, int *total_out);
BreResult match_group(MatchContext *ctx, BreMatch *m, int *total_out);

#endif /* BRE_H */
