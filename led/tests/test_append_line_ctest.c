#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ctest/ctest.h"
#include "../led.h"

static char* dup_cstr(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    memcpy(p, s, n);
    return p;
}

static void feed_stdin(const char* content) {
#if defined(_GNU_SOURCE) || defined(__APPLE__)
    FILE* mem = fmemopen((void*)content, strlen(content), "r");
    if (mem) {
        freopen(NULL, "r", stdin);
        *stdin = *mem;
    }
#else
    FILE* tmp = fopen("append_input.txt", "wb");
    fwrite(content, 1, strlen(content), tmp);
    fclose(tmp);
    freopen("append_input.txt", "rb", stdin);
#endif
}

static void common_setup(CTest *ctest) {
    (void)ctest;
}
static void common_teardown(CTest *ctest) {
    (void)ctest;
}

CTEST_TEST_SIMPLE(append_after_address) {
    common_setup(ctest);
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(3 * sizeof(char*));
    ed.lines[0] = dup_cstr("A");
    ed.lines[1] = dup_cstr("B");
    ed.lines[2] = dup_cstr("C");
    ed.num_lines = 3;
    ed.current_line = 2;

    feed_stdin("X1\nX2\n.\n");
    append_line(&ed, 0);

    CTEST_ASSERT_EQ(ed.num_lines, 5, "num_lines should be 5 after append");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "A", "line 0 should be A");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "X1", "line 1 should be X1");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "X2", "line 2 should be X2");
    CTEST_ASSERT_STR_EQ(ed.lines[3], "B", "line 3 should be B");
    CTEST_ASSERT_STR_EQ(ed.lines[4], "C", "line 4 should be C");

    free_editor(&ed);
    common_teardown(ctest);
}

CTEST_TEST_SIMPLE(append_at_end) {
    common_setup(ctest);
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(2 * sizeof(char*));
    ed.lines[0] = dup_cstr("L1");
    ed.lines[1] = dup_cstr("L2");
    ed.num_lines = 2;
    ed.current_line = 1;

    feed_stdin("N1\nN2\n.\n");
    append_line(&ed, 1);

    CTEST_ASSERT_EQ(ed.num_lines, 4, "num_lines should be 4");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "L1", "line 0");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "L2", "line 1");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "N1", "line 2");
    CTEST_ASSERT_STR_EQ(ed.lines[3], "N2", "line 3");

    free_editor(&ed);
    common_teardown(ctest);
}

CTEST_TEST_SIMPLE(empty_buffer_append) {
    Editor ed; init_editor(&ed);

    feed_stdin("First\nSecond\n.\n");
    append_line(&ed, -1);

    CTEST_ASSERT_EQ(ed.num_lines, 2, "num_lines should be 2 for empty buffer append");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "First", "line 0 should be First");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Second", "line 1 should be Second");

    free_editor(&ed);
}

int main(void) {
    ctest_run_all();
    return 0;
}

// New test ensuring current_line unchanged when no lines appended and address differs
CTEST_TEST_SIMPLE(append_no_input_address_unchanged) {
    Editor ed; init_editor(&ed);
    // Seed buffer with two lines; set current line to second line
    ed.lines = (char**)malloc(2 * sizeof(char*));
    ed.lines[0] = dup_cstr("Line1");
    ed.lines[1] = dup_cstr("Line2");
    ed.num_lines = 2;
    ed.current_line = 1; // current address is line 2

    // Simulate address 1 append (after first line) but no input (just '.')
    feed_stdin(".\n");
    append_line(&ed, 0); // address index 0 (line 1)

    CTEST_ASSERT_EQ(ed.num_lines, 2, "num_lines unchanged");
    CTEST_ASSERT_EQ(ed.current_line, 1, "current_line unchanged per POSIX ed");

    free_editor(&ed);
}

// New test ensuring current_line unchanged for insert with no input when address differs
CTEST_TEST_SIMPLE(insert_no_input_address_unchanged) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(2 * sizeof(char*));
    ed.lines[0] = dup_cstr("A");
    ed.lines[1] = dup_cstr("B");
    ed.num_lines = 2;
    ed.current_line = 0; // current address is line 1

    // Simulate insert before second line (address index 1) but no input
    feed_stdin(".\n");
    insert_line(&ed, 1);

    CTEST_ASSERT_EQ(ed.num_lines, 2, "num_lines unchanged");
    CTEST_ASSERT_EQ(ed.current_line, 1, "current_line unchanged per POSIX ed");

    free_editor(&ed);
}

