#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "led.h"
#include "ctest.h"

// Test: parse_address with empty buffer
CTEST_TEST_SIMPLE(parse_address_empty_buffer) {
    Editor ed;
    init_editor(&ed);
    
    // Empty string should return 0 for empty buffer
    int addr = parse_address(&ed, "");
    CTEST_ASSERT_EQ(addr, 0, "empty address with empty buffer should be 0");
    
    // "." (current line) should return 0 for empty buffer
    addr = parse_address(&ed, ".");
    CTEST_ASSERT_EQ(addr, 0, "current line with empty buffer should be 0");
    
    // "$" (last line) should return -1 for empty buffer
    addr = parse_address(&ed, "$");
    CTEST_ASSERT_EQ(addr, -1, "last line with empty buffer should be -1");
    
    // Any numeric address should be invalid for empty buffer
    addr = parse_address(&ed, "1");
    CTEST_ASSERT_EQ(addr, -1, "line 1 with empty buffer should be invalid");
    
    free_editor(&ed);
}

// Test: parse_address with single line buffer
CTEST_TEST_SIMPLE(parse_address_single_line) {
    Editor ed;
    init_editor(&ed);
    
    // Add one line
    ed.lines = malloc(1 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.num_lines = 1;
    ed.current_line = 1;
    
    // Empty string should return current line (0-based: 0)
    int addr = parse_address(&ed, "");
    CTEST_ASSERT_EQ(addr, 0, "empty address should return current line");
    
    // "." should return current line
    addr = parse_address(&ed, ".");
    CTEST_ASSERT_EQ(addr, 0, "current line should be 0");
    
    // "$" should return last line
    addr = parse_address(&ed, "$");
    CTEST_ASSERT_EQ(addr, 0, "last line should be 0");
    
    // "1" should return line 1 (0-based: 0)
    addr = parse_address(&ed, "1");
    CTEST_ASSERT_EQ(addr, 0, "line 1 should be 0");
    
    // "2" should be invalid
    addr = parse_address(&ed, "2");
    CTEST_ASSERT_EQ(addr, -1, "line 2 should be invalid");
    
    // "0" should be invalid
    addr = parse_address(&ed, "0");
    CTEST_ASSERT_EQ(addr, -1, "line 0 should be invalid");
    
    free_editor(&ed);
}

// Test: parse_address with multiple lines
CTEST_TEST_SIMPLE(parse_address_multiple_lines) {
    Editor ed;
    init_editor(&ed);
    
    // Add three lines
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.lines[2] = strdup("Line 3");
    ed.num_lines = 3;
    ed.current_line = 2;  // Current is line 2
    
    // Empty string should return current line (0-based: 1)
    int addr = parse_address(&ed, "");
    CTEST_ASSERT_EQ(addr, 1, "empty address should return current line (1)");
    
    // "." should return current line
    addr = parse_address(&ed, ".");
    CTEST_ASSERT_EQ(addr, 1, "current line should be 1");
    
    // "$" should return last line
    addr = parse_address(&ed, "$");
    CTEST_ASSERT_EQ(addr, 2, "last line should be 2");
    
    // "1" should return line 1 (0-based: 0)
    addr = parse_address(&ed, "1");
    CTEST_ASSERT_EQ(addr, 0, "line 1 should be 0");
    
    // "2" should return line 2 (0-based: 1)
    addr = parse_address(&ed, "2");
    CTEST_ASSERT_EQ(addr, 1, "line 2 should be 1");
    
    // "3" should return line 3 (0-based: 2)
    addr = parse_address(&ed, "3");
    CTEST_ASSERT_EQ(addr, 2, "line 3 should be 2");
    
    // "4" should be invalid
    addr = parse_address(&ed, "4");
    CTEST_ASSERT_EQ(addr, -1, "line 4 should be invalid");
    
    // "0" should be invalid
    addr = parse_address(&ed, "0");
    CTEST_ASSERT_EQ(addr, -1, "line 0 should be invalid");
    
    // Negative numbers should be invalid
    addr = parse_address(&ed, "-1");
    CTEST_ASSERT_EQ(addr, -1, "negative line should be invalid");
    
    free_editor(&ed);
}

// Test: parse_address with current_line at different positions
CTEST_TEST_SIMPLE(parse_address_current_line_positions) {
    Editor ed;
    init_editor(&ed);
    
    // Add five lines
    ed.lines = malloc(5 * sizeof(char*));
    for (int i = 0; i < 5; i++) {
        char buf[20];
        snprintf(buf, sizeof(buf), "Line %d", i + 1);
        ed.lines[i] = strdup(buf);
    }
    ed.num_lines = 5;
    
    // Test with current_line = 1 (first line)
    ed.current_line = 1;
    int addr = parse_address(&ed, ".");
    CTEST_ASSERT_EQ(addr, 0, "current line 1 should be 0");
    
    // Test with current_line = 3 (middle line)
    ed.current_line = 3;
    addr = parse_address(&ed, ".");
    CTEST_ASSERT_EQ(addr, 2, "current line 3 should be 2");
    
    // Test with current_line = 5 (last line)
    ed.current_line = 5;
    addr = parse_address(&ed, ".");
    CTEST_ASSERT_EQ(addr, 4, "current line 5 should be 4");
    
    free_editor(&ed);
}

// Test: parse_address with NULL pointer
CTEST_TEST_SIMPLE(parse_address_null) {
    Editor ed;
    init_editor(&ed);
    
    // Add one line
    ed.lines = malloc(1 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.num_lines = 1;
    ed.current_line = 1;
    
    // NULL should behave like empty string
    int addr = parse_address(&ed, NULL);
    CTEST_ASSERT_EQ(addr, 0, "NULL address should return current line");
    
    free_editor(&ed);
}

// Test: parse_address with invalid formats
CTEST_TEST_SIMPLE(parse_address_invalid_formats) {
    Editor ed;
    init_editor(&ed);
    
    // Add three lines
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.lines[2] = strdup("Line 3");
    ed.num_lines = 3;
    ed.current_line = 2;
    
    // Non-numeric, non-special strings should be treated as 0 by atoi
    // atoi("abc") returns 0, which is invalid
    int addr = parse_address(&ed, "abc");
    CTEST_ASSERT_EQ(addr, -1, "non-numeric should be invalid");
    
    // Mixed alphanumeric: atoi stops at first non-digit
    addr = parse_address(&ed, "2abc");
    CTEST_ASSERT_EQ(addr, 1, "2abc should parse as 2");
    
    free_editor(&ed);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ctest_run_all();
    return 0;
}
