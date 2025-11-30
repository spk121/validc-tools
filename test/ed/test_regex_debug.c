#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ed.h"
#include "ctest.h"

// Test: backward regex search with offset (?o?-1)
CTEST_TEST_SIMPLE(regex_backward_search_with_offset) {
    Editor ed;
    init_editor(&ed);
    ed.lines = (char **)malloc(5 * sizeof(char *));
    ed.lines[0] = strdup("one");
    ed.lines[1] = strdup("two");
    ed.lines[2] = strdup("three");
    ed.lines[3] = strdup("four");
    ed.lines[4] = strdup("five");
    ed.num_lines = 5;
    ed.current_line = 4; // on 'five' (0-indexed)

    // Test ?o?-1 search from line 'five'
    // Should find 'four' (contains 'o'), then apply -1 offset to get 'three'
    int idx = parse_address(&ed, "?o?-1");
    CTEST_ASSERT_EQ(idx, 2, "?o?-1 from line 5 should find 'four' at index 3, then -1 gives index 2 ('three')");
    
    if (idx >= 0 && idx < ed.num_lines) {
        CTEST_ASSERT_STR_EQ(ed.lines[idx], "three", "result should be 'three'");
    }

    free_editor(&ed);
}

// Test: backward regex search without offset (?o?)
CTEST_TEST_SIMPLE(regex_backward_search_no_offset) {
    Editor ed;
    init_editor(&ed);
    ed.lines = (char **)malloc(5 * sizeof(char *));
    ed.lines[0] = strdup("one");
    ed.lines[1] = strdup("two");
    ed.lines[2] = strdup("three");
    ed.lines[3] = strdup("four");
    ed.lines[4] = strdup("five");
    ed.num_lines = 5;
    ed.current_line = 4; // on 'five' (0-indexed)

    // Test ?o? search from line 'five'
    // Should find 'four' (contains 'o') searching backward from current
    int idx = parse_address(&ed, "?o?");
    CTEST_ASSERT_EQ(idx, 3, "?o? from line 5 should find 'four' at index 3");
    
    if (idx >= 0 && idx < ed.num_lines) {
        CTEST_ASSERT_STR_EQ(ed.lines[idx], "four", "result should be 'four'");
    }

    free_editor(&ed);
}

// Test: backward search wrapping around
CTEST_TEST_SIMPLE(regex_backward_search_wrap) {
    Editor ed;
    init_editor(&ed);
    ed.lines = (char **)malloc(5 * sizeof(char *));
    ed.lines[0] = strdup("one");
    ed.lines[1] = strdup("two");
    ed.lines[2] = strdup("three");
    ed.lines[3] = strdup("four");
    ed.lines[4] = strdup("five");
    ed.num_lines = 5;
    ed.current_line = 0; // on 'one' (0-indexed)

    // Test ?o? search from line 'one'
    // Should wrap around and find 'four' (last occurrence before wrapping)
    int idx = parse_address(&ed, "?o?");
    CTEST_ASSERT_EQ(idx, 3, "?o? from line 1 should wrap and find 'four' at index 3");

    free_editor(&ed);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ctest_run_all();
    return 0;
}
