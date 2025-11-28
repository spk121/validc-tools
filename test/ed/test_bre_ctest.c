// Tests for minimal BRE engine
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ctest.h"
#include "../../level1/bre.h"

CTEST_TEST_SIMPLE(literal_match) {
    BreMatch m;
    CTEST_ASSERT(bre_match("hello", "hello", &m), "exact literal");
    CTEST_ASSERT_EQ(m.start, 0, "start 0");
    CTEST_ASSERT_EQ(m.length, 5, "length 5");
}

CTEST_TEST_SIMPLE(dot_and_star) {
    BreMatch m;
    CTEST_ASSERT(bre_match("hello", "he.*o", &m), "dot star match");
    CTEST_ASSERT_EQ(m.start, 0, "start 0");
}

CTEST_TEST_SIMPLE(anchors) {
    BreMatch m;
    CTEST_ASSERT(bre_match("hello", "^he.*o$", &m), "anchored match");
    CTEST_ASSERT(!bre_match("xhello", "^he.*o$", &m), "anchored fail");
}

CTEST_TEST_SIMPLE(char_class_basic) {
    BreMatch m;
    CTEST_ASSERT(bre_match("abc", "[a-c]*", &m), "range class star");
    CTEST_ASSERT(bre_match("x", "[^a-c]", &m), "negated class");
}

CTEST_TEST_SIMPLE(escaped_literals) {
    BreMatch m;
    CTEST_ASSERT(bre_match("a.b", "a\\.b", &m), "escaped dot");
    CTEST_ASSERT(!bre_match("axb", "a\\.b", &m), "escaped mismatch");
}

CTEST_TEST_SIMPLE(simple_group_and_substitute) {
    BreMatch m;
    CTEST_ASSERT(bre_match("hello", "h\\(...\\)o", &m), "group match");
    CTEST_ASSERT_EQ(m.num_groups, 1, "one group");
    char *r = bre_substitute("hello", "h\\(...\\)o", "h\\1X");
    CTEST_ASSERT_STR_EQ(r, "hellX", "substitute with backref");
    free(r);
}

int main(void) { ctest_run_all(); return 0; }
// Additional behavior-lock tests for minimal engine deviations
CTEST_TEST_SIMPLE(char_class_literal_bracket) {
    BreMatch m;
    // Pattern "[a\[]b" should match "a[b" (literal '[' inside class)
    CTEST_ASSERT(bre_match("a[b", "[a\\[]b", &m), "literal [ in class");
}

CTEST_TEST_SIMPLE(unmatched_group_is_no_match) {
    BreMatch m;
    // Unmatched "\(" or "\)" yields no match (treated as malformed, not error)
    CTEST_ASSERT(!bre_match("hello", "h\\(ell", &m), "unmatched ( no match");
    CTEST_ASSERT(!bre_match("hello", "ell\\)", &m), "unmatched ) no match");
}

