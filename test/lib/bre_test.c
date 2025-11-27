/* bre_test.c - TAP test suite for the BRE engine (updated for BreResult API) */
#include "bre.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OK(cond, desc)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        tests++;                                                                                                       \
        if (cond)                                                                                                      \
        {                                                                                                              \
            printf("ok %d - %s\n", tests, (desc));                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            printf("not ok %d - %s\n", tests, (desc));                                                                 \
            failed = 1;                                                                                                \
        }                                                                                                              \
    } while (0)

static int tests = 0;
static int failed = 0;

static int find_group_end_test(const char *pat, int pi, int pend)
{
    /* pat[pi] == '\\', pat[pi+1] == '(' */
    for (int i = pi + 2; i < pend - 1; i++)
    {
        if (pat[i] == '\\' && pat[i + 1] == ')')
            return i;
    }
    return -1;
}

static void test_internal_groups(void)
{
    /* 1) Without quantifier: simple year capture only */
    {
        const char *text = "2025";
        const char *pat = "\\([0-9]\\{4\\}\\)";
        int pend = (int)strlen(pat);
        int pi = 0;
        int gend = find_group_end_test(pat, pi, pend);
        int inner_start = pi + 2;
        int inner_end = gend;
        int atom_end = gend + 2;

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = match_group_without_quantifier(&ctx, &m, inner_start, inner_end, atom_end, &total);

        OK(r == BRE_OK && total == 4 && m.num_groups >= 1 && m.groups[0].start == 0 && m.groups[0].length == 4,
           "match_group_without_quantifier: [0-9]{4} at start");
    }

    /* 2) Without quantifier + remainder: year followed by -xx */
    {
        const char *text = "2025-xx";
        const char *pat = "\\([0-9]\\{4\\}\\)-xx";
        int pend = (int)strlen(pat);
        int pi = 0;
        int gend = find_group_end_test(pat, pi, pend);
        int inner_start = pi + 2;
        int inner_end = gend;
        int atom_end = gend + 2;

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = match_group_without_quantifier(&ctx, &m, inner_start, inner_end, atom_end, &total);

        OK(r == BRE_OK && total == 7 && m.num_groups >= 1 && m.groups[0].start == 0 && m.groups[0].length == 4,
           "match_group_without_quantifier: [0-9]{4}-xx");
    }

    {
        const char *text = "xx-2025";
        const char *pat = "\\([0-9]\\{4\\}\\)";
        int pend = (int)strlen(pat);
        int pi = 0;
        int gend = find_group_end_test(pat, pi, pend);
        int inner_start = pi + 2;
        int inner_end = gend;
        int atom_end = gend + 2;

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = match_group_without_quantifier(&ctx, &m, inner_start, inner_end, atom_end, &total);

        OK(r == BRE_OK && total == 7 && m.num_groups >= 1 && m.groups[0].start == 3 && m.groups[0].length == 4,
           "match_group_without_quantifier skip prefix: [0-9]{4}");
    }

    /* 3) Without quantifier: \(.*\) backtracking against remainder */
    {
        const char *text = "John Doe";
        const char *pat = "\\(.*\\) Doe";
        int pend = (int)strlen(pat);
        int pi = 0;
        int gend = find_group_end_test(pat, pi, pend);
        int inner_start = pi + 2;
        int inner_end = gend;
        int atom_end = gend + 2;

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = match_group_without_quantifier(&ctx, &m, inner_start, inner_end, atom_end, &total);

        OK(r == BRE_OK && total == (int)strlen(text) && m.num_groups >= 1 && m.groups[0].start == 0 &&
               m.groups[0].length == 4,
           "match_group_without_quantifier: (.*) backtracking capture");
    }

    /* 4) With quantifier: \(aa\)\{2\}bb on 'aaaabb' */
    {
        const char *text = "aaaabb";
        const char *pat = "\\(aa\\)\\{2\\}bb";
        int pend = (int)strlen(pat);
        int pi = 0;
        int gend = find_group_end_test(pat, pi, pend);
        int inner_start = pi + 2;
        int inner_end = gend;
        int atom_start = pi;
        int atom_end = gend + 2;

        BreRepetition rep = {0};
        BreResult pr = parse_bre_repetition(pat, atom_end, pend, &rep);

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = (pr == BRE_OK) ? match_group_with_quantifier(&ctx, &m, inner_start, inner_end, atom_start,
                                                                   atom_end, &rep, &total)
                                     : pr;

        OK(r == BRE_OK && total == 6 && m.num_groups >= 1 && m.groups[0].start == 0 && m.groups[0].length == 4,
           "match_group_with_quantifier: (aa){2}bb");
    }

    {
        const char *text = "123x";
        const char *pat = "\\([0-9]\\{1,\\}\\)x";
        int pend = (int)strlen(pat);
        int pi = 0;
        int gend = find_group_end_test(pat, pi, pend);
        int inner_start = pi + 2;
        int inner_end = gend;
        int atom_start = pi;
        int atom_end = gend + 2;

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = match_group_without_quantifier(&ctx, &m, inner_start, inner_end, atom_end, &total);

        OK(r == BRE_OK && total == 4 && m.num_groups >= 1 && m.groups[0].start == 0 && m.groups[0].length == 3,
           "match_group_without_quantifier: ([0-9]{1,})x");
    }

    {
        const char *text = "123x";
        const char *pat = "\\([0-9]\\{1,\\}\\)\\+x";
        int pend = (int)strlen(pat);
        int pi = 0;

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = match_group(&ctx, &m, &total);

        OK(r == BRE_OK && total == 4 && m.num_groups >= 1 && m.groups[0].start == 0 && m.groups[0].length == 3,
           "match_group: ([0-9]{1,})+x");
    }

    /* 6) Without quantifier: group at pattern start must scan forward in text */
    {
        const char *text = "xx-2025";
        const char *pat = "\\([0-9]\\{4\\}\\)";
        int pend = (int)strlen(pat);
        int pi = 0;
        int gend = find_group_end_test(pat, pi, pend);
        int inner_start = pi + 2;
        int inner_end = gend;
        int atom_end = gend + 2;

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = match_group_without_quantifier(&ctx, &m, inner_start, inner_end, atom_end, &total);

        OK(r == BRE_OK && m.groups[0].start == 3 /* start at '2' index */
               && m.groups[0].length == 4 && total == (3 /* skipped */ + 4 /* group */),
           "match_group_without_quantifier: scans forward to digits in text");
    }

    /* 7) Without quantifier: inner has repetition; ensure handled inside group */
    {
        const char *text = "123x";
        const char *pat = "\\([0-9]\\{1,\\}\\)x";
        int pend = (int)strlen(pat);
        int pi = 0;
        int gend = find_group_end_test(pat, pi, pend);
        int inner_start = pi + 2;
        int inner_end = gend;
        int atom_end = gend + 2;

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = match_group_without_quantifier(&ctx, &m, inner_start, inner_end, atom_end, &total);

        OK(r == BRE_OK && m.groups[0].start == 0 && m.groups[0].length == 3 /* inner quantifier consumes 123 */
               && total == 4,                                               /* 3 digits + 'x' */
           "match_group_without_quantifier: inner repetition [0-9]{1,} handled");
    }

    /* 8) With quantifier: ensure group-level {n,m} repetition captures full span */
    {
        const char *text = "aaaa--";
        const char *pat = "\\(aa\\)\\{2\\}--";
        int pend = (int)strlen(pat);
        int pi = 0;
        int gend = find_group_end_test(pat, pi, pend);
        int inner_start = pi + 2;
        int inner_end = gend;
        int atom_start = pi;
        int atom_end = gend + 2;

        BreRepetition rep = {0};
        BreResult pr = parse_bre_repetition(pat, atom_end, pend, &rep);

        MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pat, .pi = pi, .pend = pend};
        BreMatch m = {0};
        int total = -1;
        BreResult r = (pr == BRE_OK) ? match_group_with_quantifier(&ctx, &m, inner_start, inner_end, atom_start,
                                                                   atom_end, &rep, &total)
                                     : pr;

        OK(r == BRE_OK && m.groups[0].start == 0 && m.groups[0].length == 4 /* (aa){2} = "aaaa" */
               && total == 6,                                               /* 4 for group + 2 for "--" */
           "match_group_with_quantifier: captures full repeated span");
    }

    /* 9) Group at pattern start should allow bre_match to advance text start */
    {
        BreMatch m = (BreMatch){0};
        BreResult r = bre_match("xx 2025-11-26", "\\([0-9]\\{4\\}\\)-\\([0-9]\\{2\\}\\)-\\([0-9]\\{2\\}\\)", &m);
        OK(r == BRE_OK && m.num_groups == 3 && m.groups[0].start == 3 /* start of 2025 after "xx " */
               && m.groups[0].length == 4 && m.groups[1].start == 8 && m.groups[2].start == 11,
           "bre_match advances past non-matching prefix when pattern starts with group");
    }
}

/* --------------------------------------------------------------
   Dedicated tests for parse_bre_repetition()
   -------------------------------------------------------------- */
static void test_parse_bre_repetition(void)
{
    BreRepetition rep;

/* Helper macro to reduce boilerplate */
#define TEST_PAT(pat_str, start_idx, expected_result, ...)                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        const char *pat = (pat_str);                                                                                   \
        int pend = (int)strlen(pat);                                                                                   \
        int pi = (start_idx);                                                                                          \
        BreResult res = parse_bre_repetition(pat, pi, pend, &rep);                                                     \
        OK(res == (expected_result), __VA_ARGS__);                                                                     \
    } while (0)

    /* 1. Valid cases */
    TEST_PAT("a\\{5\\}x", 1, BRE_OK, "valid \\{5\\} → exact 5");
    OK(rep.min == 5 && rep.max == 5 && rep.next_pi == 6, "exact: min=5, max=5, next_pi=6");

    TEST_PAT("b\\{3,7\\}y", 1, BRE_OK, "valid \\{3,7\\}");
    OK(rep.min == 3 && rep.max == 7 && rep.next_pi == 8, "bounded: min=3, max=7");

    TEST_PAT("c\\{10,\\}z", 1, BRE_OK, "valid \\{10,\\} → 10 or more");
    OK(rep.min == 10 && rep.max == -1 && rep.next_pi == 8, "unbounded upper");

    TEST_PAT("\\{0,\\}", 0, BRE_OK, "\\{0,\\} at start");
    OK(rep.min == 0 && rep.max == -1, "zero or more allowed");

    TEST_PAT("xyz\\{42\\}", 3, BRE_OK, "repetition not at start of string");
    OK(rep.min == 42 && rep.max == 42 && rep.next_pi == 9, "offset works");

    /* 2. Malformed cases (must return BRE_ERROR) */
    TEST_PAT("a\\{5", 1, BRE_ERROR, "missing closing \\}");
    TEST_PAT("a\\{5\\", 1, BRE_ERROR, "missing } after \\");
    TEST_PAT("a\\{5}x", 1, BRE_ERROR, "no closing \\}");
    TEST_PAT("a\\{abc\\}", 1, BRE_ERROR, "non-digit in number");
    TEST_PAT("a\\{,5\\}", 1, BRE_ERROR, "missing min");
    TEST_PAT("a\\{5,\\", 1, BRE_ERROR, "truncated after comma");
    TEST_PAT("a\\{5,abc\\}", 1, BRE_ERROR, "letters in max");
    TEST_PAT("a\\{5,3\\}", 1, BRE_ERROR, "max < min should be rejected? (we allow, but atoi gives 3)");

    /* 3. Not a repetition (return BRE_NOMATCH) */
    TEST_PAT("abc", 0, BRE_NOMATCH, "no \\{ at all");
    TEST_PAT("a{5\\}", 1, BRE_NOMATCH, "missing opening backslash");
    TEST_PAT("a\\{5\\}x", 0, BRE_NOMATCH, "pi=0 points to 'a', not \\{ -> return BRE_NOMATCH");

    /* 4. Edge cases */
    TEST_PAT("\\{255\\}", 0, BRE_OK, "equal to RE_DUP_MAX");
    OK(rep.min == 255 && rep.max == 255, "RE_DUP_MAX parsed");
    TEST_PAT("\\{999999999\\}", 0, BRE_ERROR, "greater than RE_DUP_MAX");

    TEST_PAT("\\{0\\}", 0, BRE_OK, "\\{0\\} → zero times");
    OK(rep.min == 0 && rep.max == 0, "zero repetition");

    TEST_PAT("abc\\{1,1\\}def", 3, BRE_OK, "repetition in middle");
    OK(rep.min == 1 && rep.max == 1 && rep.next_pi == 10, "middle position");

    printf("1..%d\n", tests); /* Provide a plan for this block only (legacy style) */
}

static void test_match(void)
{
    BreMatch m = {0};
    BreResult r;

    /* 1-10: basic matching */
    r = bre_match("hello world", "hello", &m);
    OK(r == BRE_OK && m.start == 0 && m.length == 5, "literal match");

    r = bre_match("hello world", "world", &m);
    OK(r == BRE_OK && m.start == 6 && m.length == 5, "literal at offset");

    r = bre_match("hello", "world", &m);
    OK(r == BRE_NOMATCH, "no match");

    r = bre_match("abc", ".", &m);
    OK(r == BRE_OK && m.length == 1, "dot matches any");

    r = bre_match("abc", "^a", &m);
    OK(r == BRE_OK && m.start == 0, "caret anchor");

    r = bre_match("abc", "c$", &m);
    OK(r == BRE_OK && m.start == 2, "dollar anchor");

    r = bre_match("abc", "^abc$", &m);
    OK(r == BRE_OK && m.start == 0 && m.length == 3, "full line anchor");

    r = bre_match("xyz", "[xyz]", &m);
    OK(r == BRE_OK, "character class");

    r = bre_match("abc", "[xyz]", &m);
    OK(r == BRE_NOMATCH, "character class no match");

    r = bre_match("abc", "[^xyz]", &m);
    OK(r == BRE_OK, "negated class");

    /* 11-20: repetitions (BRE real syntax) */
    r = bre_match("aaabc", "a\\{3\\}", &m);
    OK(r == BRE_OK && m.length == 3, "exact repetition {3}");

    r = bre_match("aaaaabc", "a\\{3,5\\}", &m);
    OK(r == BRE_OK && m.length == 5, "bounded repetition {3,5}");

    r = bre_match("abc", "a\\{0,5\\}", &m);
    OK(r == BRE_OK, "zero-to-five");

    r = bre_match("aaaaaaaabc", "a\\{5,\\}", &m);
    OK(r == BRE_OK && m.length == 8, "five or more");

    /* 21-30: capture groups */
    memset(&m, 0xFF, sizeof(m)); /* poison to detect uninitialized fields */
    r = bre_match("date: 2025-11-26", "\\([0-9]\\{4\\}\\)-\\([0-9]\\{2\\}\\)-\\([0-9]\\{2\\}\\)", &m);
    OK(r == BRE_OK && m.num_groups == 3 && m.groups[0].start == 6 && m.groups[0].length == 4 &&
           m.groups[1].start == 11 && m.groups[1].length == 2 && m.groups[2].start == 14 && m.groups[2].length == 2,
       "capture groups \\(year\\)-\\(month\\)-\\(day\\)");

    r = bre_match("foo123bar456baz", "foo\\([0-9]\\{3\\}\\)bar", &m);
    OK(r == BRE_OK && m.num_groups >= 1 && m.groups[0].start == 3 && m.groups[0].length == 3, "one capture group");

    r = bre_match("aaaa", "\\(aa\\)\\{2\\}", &m);
    OK(r == BRE_OK && m.num_groups == 1 && m.groups[0].start == 0 && m.groups[0].length == 4,
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
    printf("1..%d\n", 49); /* Original plan retained (legacy); counts include all OK() calls */

    test_internal_groups();
    test_match();
    test_substitute();
    test_parse_bre_repetition();

#if 0
    /* Extra sanity checks updating to BreResult */
    BreMatch m = {0};
    BreResult r;
    r = bre_match("", "", &m);
    OK(r == BRE_NOMATCH, "empty pattern on empty string is false in most BRE impls");
    r = bre_match("abc", "abc", &m);
    OK(r == BRE_OK && m.start == 0 && m.length == 3, "basic full match sets start/length");
#endif

    return failed ? 1 : 0;
}

#if 0
/* Legacy example main preserved (would need updating to BreResult if re-enabled). */
#include "bre.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const char *text1 = "This is foo in a string";
    const char *pattern1 = "foo";
    const char *replacement1 = "bar";

    printf("Test 1: Simple replacement\n");

    BreMatch match1;
    if (bre_match(text1, pattern1, &match1) == BRE_OK) {
        printf("Match found at position %d, length %d\n", match1.start, match1.length);
    } else {
        printf("No match found\n");
    }

    char *result1 = bre_substitute(text1, pattern1, replacement1);
    if (result1) {
        printf("Result: %s\n", result1);
        free(result1);
    }

    const char *text2 = "This is foo in a string";
    const char *pattern2 = "\\(foo\\)";
    const char *replacement2 = "\\1bar";

    BreMatch match2;
    if (bre_match(text2, pattern2, &match2) == BRE_OK) {
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
    }

    return 0;
}
#endif
