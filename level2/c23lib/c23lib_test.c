#include "../ctest/ctest.h"
#include "c23lib.h"

CTEST_TEST(test_strdup) {
    char *s = c23_strdup("hello");
    CTEST_ASSERT_NOT_NULL(s, "strdup should allocate");
    CTEST_ASSERT_STR_EQ(s, "hello", "strdup should copy");
    free(s);
}

CTEST_TEST(test_c_strcasecmp) {
    CTEST_ASSERT_EQ(c23_c_strcasecmp("Hello", "HELLO"), 0, "C-locale case-insensitive match");
    CTEST_ASSERT_NE(c23_c_strcasecmp("Hello", "world"), 0, "C-locale mismatch");
    CTEST_ASSERT_EQ(c23_c_strcasecmp("abc", "ABC"), 0, "C-locale multi-char match");
}

CTEST_TEST(test_strsep) {
    char input[] = "one,two,,three";
    char *p = input;
    char *tok = c23_strsep(&p, ",");
    CTEST_ASSERT_STR_EQ(tok, "one", "First token");
    tok = c23_strsep(&p, ",");
    CTEST_ASSERT_STR_EQ(tok, "two", "Second token");
    tok = c23_strsep(&p, ",");
    CTEST_ASSERT_STR_EQ(tok, "", "Empty token");
}

int main(void) {
    ctest_run_all();
    return 0;
}
