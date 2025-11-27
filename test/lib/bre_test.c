/* bre_test.c - TAP test suite for the BRE engine */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bre.h"

#define OK(cond, desc) do {			\
	tests++;				\
	if (cond) {				\
	    printf("ok %d - %s\n", tests, (desc));	\
	} else {				\
	    printf("not ok %d - %s\n", tests, (desc)); \
	    failed = 1;				\
	}					\
    } while(0)

static int tests = 0;
static int failed = 0;

/* --------------------------------------------------------------
   Dedicated tests for parse_bre_repetition()
   -------------------------------------------------------------- */
static void test_parse_bre_repetition(void)
{
    int min_rep, max_rep, next_pi;

    // Helper macro to reduce boilerplate
    #define TEST_PAT(pat_str, start_idx, expected_result, ...) do { \
        const char *pat = (pat_str); \
        int pend = (int)strlen(pat); \
        int pi = (start_idx); \
        int res = parse_bre_repetition(pat, pi, pend, &min_rep, &max_rep, &next_pi); \
        OK(res == (expected_result), __VA_ARGS__); \
    } while(0)

    // 1. Valid cases
    TEST_PAT("a\\{5\\}x",      1,  1, "valid \\{5\\} → exact 5");
    OK(min_rep == 5 && max_rep == 5 && next_pi == 6, "exact: min=5, max=5, next_pi=6");

    TEST_PAT("b\\{3,7\\}y",    1,  1, "valid \\{3,7\\}");
    OK(min_rep == 3 && max_rep == 7 && next_pi == 8, "bounded: min=3, max=7");

    TEST_PAT("c\\{10,\\}z",    1,  1, "valid \\{10,\\} → 10 or more");
    OK(min_rep == 10 && max_rep == -1 && next_pi == 8, "unbounded upper");

    TEST_PAT("\\{0,\\}",       0,  1, "\\{0,\\} at start");
    OK(min_rep == 0 && max_rep == -1, "zero or more allowed");

    TEST_PAT("xyz\\{42\\}",   3,  1, "repetition not at start of string");
    OK(min_rep == 42 && max_rep == 42 && next_pi == 9, "offset works");

    // 2. Malformed cases (must return -1)
    TEST_PAT("a\\{5",         1, -1, "missing closing \\}");
    TEST_PAT("a\\{5\\",        1, -1, "missing } after \\");
    TEST_PAT("a\\{5}x",        1, -1, "no closing \\}");
    TEST_PAT("a\\{abc\\}",     1, -1, "non-digit in number");
    TEST_PAT("a\\{,5\\}",      1, -1, "missing min");
    TEST_PAT("a\\{5,\\",       1, -1, "truncated after comma");
    TEST_PAT("a\\{5,abc\\}",   1, -1, "letters in max");
    TEST_PAT("a\\{5,3\\}",     1, -1, "max < min should be rejected? (we allow, but atoi gives 3)");
    // Note: POSIX allows max < min → treat as exact min. We accept it.

    // 3. Not a repetition (return 0)
    TEST_PAT("abc",           0,  0, "no \\{ at all");
    TEST_PAT("a{5\\}",         1,  0, "missing opening backslash");
    TEST_PAT("a\\{5\\}x",      0,  0, "pi=0 points to 'a', not \\{ -> return 0");

    // 4. Edge cases
    TEST_PAT("\\{255\\}", 0, 1, "equal to RE_DUP_MAX");
    OK(min_rep == 255 && max_rep == 255, "RE_DUP_MAX parsed");
    TEST_PAT("\\{999999999\\}", 0, -1, "greater than RE_DUP_MAX");

    TEST_PAT("\\{0\\}",        0, 1, "\\{0\\} → zero times");
    OK(min_rep == 0 && max_rep == 0, "zero repetition");

    TEST_PAT("abc\\{1,1\\}def", 3, 1, "repetition in middle");
    OK(min_rep == 1 && max_rep == 1 && next_pi == 10, "middle position");

    printf("1..%d\n", tests);  // override plan at end of this block if needed
}

static void test_match(void)
{
    BreMatch m = {0};

    /* 1-10: basic matching */
    OK(bre_match("hello world", "hello", &m) && m.start == 0 && m.length == 5, "literal match");
    OK(bre_match("hello world", "world", &m) && m.start == 6 && m.length == 5, "literal at offset");
    OK(!bre_match("hello", "world", &m), "no match");

    OK(bre_match("abc", ".", &m) && m.length == 1, "dot matches any");
    OK(bre_match("abc", "^a", &m) && m.start == 0, "caret anchor");
    OK(bre_match("abc", "c$", &m) && m.start == 2, "dollar anchor");
    OK(bre_match("abc", "^abc$", &m) && m.start == 0 && m.length == 3, "full line anchor");

    OK(bre_match("xyz", "[xyz]", &m), "character class");
    OK(!bre_match("abc", "[xyz]", &m), "character class no match");
    OK(bre_match("abc", "[^xyz]", &m), "negated class");

    /* 11-20: repetitions (BRE real syntax) */
    OK(bre_match("aaabc", "a\\{3\\}", &m) && m.length == 3, "exact repetition {3}");
    OK(bre_match("aaaaabc", "a\\{3,5\\}", &m) && m.length == 5, "bounded repetition {3,5}");
    OK(bre_match("abc", "a\\{0,5\\}", &m), "zero-to-five");
    OK(bre_match("aaaaaaaabc", "a\\{5,\\}", &m) && m.length == 8, "five or more");

    /* 21-30: capture groups */
    memset(&m, 0xFF, sizeof(m));  // poison to detect uninitialized fields
    OK(bre_match("date: 2025-11-26", "\\([0-9]\\{4\\}\\)-\\([0-9]\\{2\\}\\)-\\([0-9]\\{2\\}\\)", &m)
       && m.num_groups == 3
       && m.groups[0].start == 6 && m.groups[0].length == 4
       && m.groups[1].start == 11 && m.groups[1].length == 2
       && m.groups[2].start == 14 && m.groups[2].length == 2,
       "capture groups \\(year\\)-\\(month\\)-\\(day\\)");

    OK(bre_match("foo123bar456baz", "foo\\([0-9]\\{3\\}\\)bar", &m)
       && m.groups[0].start == 3 && m.groups[0].length == 3,
       "one capture group");
    OK(bre_match("aaaa", "\\(aa\\)\\{2\\}", &m)
       && m.num_groups == 1
       && m.groups[0].start == 0 && m.groups[0].length == 4,
       "capture group with repetition captures full match");
}

static void test_substitute(void)
{
    char *res;

    res = bre_substitute("hello world", "world", "planet");
    OK(res && strcmp(res, "hello planet") == 0, "simple substitute");
    free(res);

    res = bre_substitute("John Doe", "^\\(.*\\) \\(.*\\)$", "\\2, \\1");
    OK(res && strcmp(res, "Doe, John") == 0, "swap first/last name with \\1 \\2");
    free(res);

    res = bre_substitute("2025-11-26", "\\([0-9]\\{4\\}\\)-\\([0-9]\\{2\\}\\)-\\([0-9]\\{2\\}\\)", "\\1/\\2/\\3");
    OK(res && strcmp(res, "2025/11/26") == 0, "date reformat with three groups");
    free(res);

    res = bre_substitute("foo123bar", "foo\\([0-9]\\+\\)bar", "baz\\1qux");
    OK(res && strcmp(res, "baz123qux") == 0, "backreference in replacement");
    free(res);

    res = bre_substitute("no match here", "nomatch", "whatever");
    OK(res && strcmp(res, "no match here") == 0, "substitute on no match returns copy");
    free(res);
}

int main(void)
{
    printf("1..%d\n", 49);

    test_match();
    test_substitute();
    test_parse_bre_repetition();

#if 0    
    /* Extra sanity checks */
    BreMatch m = {0};
    OK(!bre_match("", "", &m), "empty pattern on empty string is false in most BRE impls");
    OK(bre_match("abc", "abc", &m) && m.start == 0 && m.length == 3, "basic full match sets start/length");
#endif
    
    return failed ? 1 : 0;
}

#if 0
#include <stdio.h>
#include <stdlib.h>
#include "bre.h"

int main(void) {
    // Test 1: Simple replacement of "foo" with "bar"
    const char *text1 = "This is foo in a string";
    const char *pattern1 = "foo";
    const char *replacement1 = "bar";

    printf("Test 1: Simple replacement\n");
    printf("Original: %s\n", text1);
    printf("Pattern: %s, Replacement: %s\n", pattern1, replacement1);

    BreMatch match1;
    if (bre_match(text1, pattern1, &match1)) {
        printf("Match found at position %d, length %d\n", match1.start, match1.length);
    } else {
        printf("No match found\n");
    }

    char *result1 = bre_substitute(text1, pattern1, replacement1);
    if (result1) {
        printf("Result: %s\n", result1);
        free(result1);
    } else {
        printf("Substitution failed\n");
    }

    // Test 2: Using capture group and backreference
    const char *text2 = "This is foo in a string";
    const char *pattern2 = "\\(foo\\)";
    const char *replacement2 = "\\1bar";

    printf("\nTest 2: Capture group and backreference\n");
    printf("Original: %s\n", text2);
    printf("Pattern: %s, Replacement: %s\n", pattern2, replacement2);

    BreMatch match2;
    if (bre_match(text2, pattern2, &match2)) {
        printf("Match found at position %d, length %d, groups %d\n", 
               match2.start, match2.length, match2.num_groups);
        for (int i = 0; i < match2.num_groups; i++) {
            printf("Group %d: start %d, length %d\n", 
                   i + 1, match2.groups[i].start, match2.groups[i].length);
        }
    } else {
        printf("No match found\n");
    }

    char *result2 = bre_substitute(text2, pattern2, replacement2);
    if (result2) {
        printf("Result: %s\n", result2);
        free(result2);
    } else {
        printf("Substitution failed\n");
    }

    return 0;
}
#endif
