#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "led.h"
#include "ctest.h"

// Test: print_line with invalid addresses
CTEST_TEST_SIMPLE(print_line_invalid_addresses) {
    Editor ed;
    init_editor(&ed);
    
    // Empty buffer - any address should fail
    print_line(&ed, 0);  // Should print "?"
    print_line(&ed, -1); // Should print "?"
    print_line(&ed, 1);  // Should print "?"
    
    // Add one line
    ed.lines = malloc(1 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.num_lines = 1;
    ed.current_line = 0;
    
    // Valid address should work (we can't easily test stdout)
    print_line(&ed, 0);  // Valid
    
    // Invalid addresses
    print_line(&ed, -1); // Should print "?"
    print_line(&ed, 1);  // Should print "?" (out of bounds)
    print_line(&ed, 10); // Should print "?"
    
    free_editor(&ed);
}

// Test: delete_line with invalid addresses
CTEST_TEST_SIMPLE(delete_line_invalid_addresses) {
    Editor ed;
    init_editor(&ed);
    
    // Empty buffer - should fail gracefully
    delete_line(&ed, 0);  // Should print "?"
    CTEST_ASSERT_EQ(ed.num_lines, 0, "empty buffer should stay empty");
    
    // Add three lines
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.lines[2] = strdup("Line 3");
    ed.num_lines = 3;
    ed.current_line = 1;
    
    // Invalid addresses should not delete anything
    delete_line(&ed, -1);
    CTEST_ASSERT_EQ(ed.num_lines, 3, "should still have 3 lines");
    
    delete_line(&ed, 3);
    CTEST_ASSERT_EQ(ed.num_lines, 3, "should still have 3 lines");
    
    delete_line(&ed, 100);
    CTEST_ASSERT_EQ(ed.num_lines, 3, "should still have 3 lines");
    
    // Valid delete
    delete_line(&ed, 1);  // Delete line 2 (0-based: 1)
    CTEST_ASSERT_EQ(ed.num_lines, 2, "should have 2 lines after delete");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "Line 1", "line 1 should be unchanged");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Line 3", "line 3 should move to position 1");
    
    free_editor(&ed);
}

// Test: parse_address validation for different buffer states
CTEST_TEST_SIMPLE(parse_address_validation_empty_buffer) {
    Editor ed;
    init_editor(&ed);
    
    // With empty buffer, most addresses should be invalid or special
    int addr;
    
    addr = parse_address(&ed, "1");
    CTEST_ASSERT_EQ(addr, -1, "line 1 invalid for empty buffer");
    
    addr = parse_address(&ed, "0");
    CTEST_ASSERT_EQ(addr, -1, "line 0 always invalid");
    
    addr = parse_address(&ed, "$");
    CTEST_ASSERT_EQ(addr, -1, "$ is -1 for empty buffer");
    
    addr = parse_address(&ed, ".");
    CTEST_ASSERT_EQ(addr, -1, ". is invalid for empty buffer");
    
    addr = parse_address(&ed, "");
    CTEST_ASSERT_EQ(addr, -1, "empty address is invalid (undefined) for empty buffer");
    
    free_editor(&ed);
}

// Test: parse_address validation for commands that need valid line numbers
CTEST_TEST_SIMPLE(parse_address_validation_commands) {
    Editor ed;
    init_editor(&ed);
    
    // Add three lines
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.lines[2] = strdup("Line 3");
    ed.num_lines = 3;
    ed.current_line = 1;
    
    int addr;
    
    // Valid addresses for print/delete (must be in range)
    addr = parse_address(&ed, "1");
    CTEST_ASSERT_EQ(addr, 0, "line 1 is valid");
    CTEST_ASSERT_TRUE(addr >= 0 && addr < ed.num_lines, "address in valid range for print");
    
    addr = parse_address(&ed, "3");
    CTEST_ASSERT_EQ(addr, 2, "line 3 is valid");
    CTEST_ASSERT_TRUE(addr >= 0 && addr < ed.num_lines, "address in valid range for print");
    
    addr = parse_address(&ed, "$");
    CTEST_ASSERT_EQ(addr, 2, "$ is last line");
    CTEST_ASSERT_TRUE(addr >= 0 && addr < ed.num_lines, "address in valid range for print");
    
    // Invalid addresses for print/delete
    addr = parse_address(&ed, "0");
    CTEST_ASSERT_EQ(addr, -1, "line 0 is invalid");
    CTEST_ASSERT_FALSE(addr >= 0 && addr < ed.num_lines, "address out of range");
    
    addr = parse_address(&ed, "4");
    CTEST_ASSERT_EQ(addr, -1, "line 4 is invalid");
    CTEST_ASSERT_FALSE(addr >= 0 && addr < ed.num_lines, "address out of range");
    
    // Relative offset: "-1" means current - 1
    // With current_line = 1 (line 2), "-1" should give line 1 (0-based: 0)
    ed.current_line = 1;
    addr = parse_address(&ed, "-1");
    CTEST_ASSERT_EQ(addr, 0, "-1 from line 2 should give line 1 (0-based: 0)");
    CTEST_ASSERT_TRUE(addr >= 0 && addr < ed.num_lines, "-1 is valid relative address");
    
    free_editor(&ed);
}

// Test: append/insert address validation (different rules)
CTEST_TEST_SIMPLE(append_insert_address_validation) {
    Editor ed;
    init_editor(&ed);
    
    // For append: -1 means append after last line (or start of empty buffer)
    // For insert: -1 means insert at start
    
    // Empty buffer: addr -1 is valid for append (means "after end")
    int addr = parse_address(&ed, "$");
    CTEST_ASSERT_EQ(addr, -1, "$ gives -1 for empty buffer");
    // append_line handles -1 specially for empty buffer
    
    // Add two lines
    ed.lines = malloc(2 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.num_lines = 2;
    ed.current_line = 0;
    
    // For append with non-empty buffer
    addr = parse_address(&ed, "1");
    CTEST_ASSERT_EQ(addr, 0, "line 1 gives 0");
    CTEST_ASSERT_TRUE(addr >= 0 && addr < ed.num_lines, "valid for append after line 1");
    
    addr = parse_address(&ed, "$");
    CTEST_ASSERT_EQ(addr, 1, "$ gives 1 (last line)");
    CTEST_ASSERT_TRUE(addr >= 0 && addr < ed.num_lines, "valid for append after last");
    
    // Edge case: appending "after line 0" is conceptually invalid in ed
    // But our implementation might accept it
    addr = parse_address(&ed, "0");
    CTEST_ASSERT_EQ(addr, -1, "line 0 is always invalid");
    
    free_editor(&ed);
}

// Test: address validation with boundary values
CTEST_TEST_SIMPLE(address_validation_boundaries) {
    Editor ed;
    init_editor(&ed);
    
    // Add 5 lines
    ed.lines = malloc(5 * sizeof(char*));
    for (int i = 0; i < 5; i++) {
        char buf[20];
        snprintf(buf, sizeof(buf), "Line %d", i + 1);
        ed.lines[i] = strdup(buf);
    }
    ed.num_lines = 5;
    ed.current_line = 2;
    
    int addr;
    
    // First valid line
    addr = parse_address(&ed, "1");
    CTEST_ASSERT_EQ(addr, 0, "first line is 0");
    print_line(&ed, addr);  // Should succeed
    
    // Last valid line
    addr = parse_address(&ed, "5");
    CTEST_ASSERT_EQ(addr, 4, "last line is 4");
    print_line(&ed, addr);  // Should succeed
    
    // Just before first (invalid)
    addr = parse_address(&ed, "0");
    CTEST_ASSERT_EQ(addr, -1, "line 0 invalid");
    print_line(&ed, addr);  // Should print "?"
    
    // Just after last (invalid)
    addr = parse_address(&ed, "6");
    CTEST_ASSERT_EQ(addr, -1, "line 6 invalid");
    print_line(&ed, addr);  // Should print "?"
    
    // Way out of bounds
    addr = parse_address(&ed, "1000");
    CTEST_ASSERT_EQ(addr, -1, "line 1000 invalid");
    
    free_editor(&ed);
}

// Test: current line behavior after operations
CTEST_TEST_SIMPLE(current_line_after_operations) {
    Editor ed;
    init_editor(&ed);
    
    // Add three lines
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.lines[2] = strdup("Line 3");
    ed.num_lines = 3;
    ed.current_line = 0;
    
    // Print updates current line
    print_line(&ed, 1);  // Print line 2 (0-based: 1)
    CTEST_ASSERT_EQ(ed.current_line, 1, "current line should be 2 after printing line 2");
    
    print_line(&ed, 2);  // Print line 3 (0-based: 2)
    CTEST_ASSERT_EQ(ed.current_line, 2, "current line should be 3 after printing line 3");
    
    // Using "." should now give line 3
    int addr = parse_address(&ed, ".");
    CTEST_ASSERT_EQ(addr, 2, ". should be 2 (line 3, 0-based)");
    
    free_editor(&ed);
}

// Test: setting current line with bare address validation
CTEST_TEST_SIMPLE(bare_address_validation) {
    Editor ed;
    init_editor(&ed);
    
    // Empty buffer - can't set to line 1
    int addr = parse_address(&ed, "1");
    CTEST_ASSERT_EQ(addr, -1, "line 1 should be invalid for empty buffer");
    // In execute_command, this would print "?"
    
    // Add three lines
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.lines[2] = strdup("Line 3");
    ed.num_lines = 3;
    ed.current_line = 0;
    
    // Valid bare address
    addr = parse_address(&ed, "2");
    CTEST_ASSERT_EQ(addr, 1, "line 2 should be valid");
    CTEST_ASSERT_TRUE(addr >= 0 && addr < ed.num_lines, "address is in range");
    
    // Invalid bare address
    addr = parse_address(&ed, "5");
    CTEST_ASSERT_EQ(addr, -1, "line 5 should be invalid");
    CTEST_ASSERT_FALSE(addr >= 0 && addr < ed.num_lines, "address is out of range");
    
    // Line 0 is always invalid
    addr = parse_address(&ed, "0");
    CTEST_ASSERT_EQ(addr, -1, "line 0 should be invalid");
    
    free_editor(&ed);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ctest_run_all();
    return 0;
}


