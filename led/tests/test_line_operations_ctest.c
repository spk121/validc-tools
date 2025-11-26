// Test line operations: c, m, t, j commands
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "led.h"
#include "ctest.h"

static char* dup_cstr(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    memcpy(p, s, n);
    return p;
}

// Test c command: change lines in range
CTEST_TEST_SIMPLE(change_command_single_line) {
    Editor ed;
    init_editor(&ed);
    
    ed.num_lines = 3;
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = dup_cstr("Line 1");
    ed.lines[1] = dup_cstr("Line 2");
    ed.lines[2] = dup_cstr("Line 3");
    ed.current_line = 1;
    
    // Change line 2 (will delete and prompt for input, which we simulate)
    AddressRange range = {1, 1};
    delete_range(&ed, range);
    
    // Manually insert new line (simulating what change_range does after delete)
    ed.num_lines = 3;
    ed.lines = realloc(ed.lines, 3 * sizeof(char*));
    for (int i = ed.num_lines - 1; i > 1; i--) {
        ed.lines[i] = ed.lines[i - 1];
    }
    ed.lines[1] = dup_cstr("Changed");
    ed.current_line = 1;
    ed.dirty = 1;
    
    CTEST_ASSERT_EQ(ed.num_lines, 3, "3 lines after change");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "Line 1", "line 0 unchanged");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Changed", "line 1 changed");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "Line 3", "line 2 unchanged");
    
    free_editor(&ed);
}

// Test m command: move lines
CTEST_TEST_SIMPLE(move_command_forward) {
    Editor ed;
    init_editor(&ed);
    
    ed.num_lines = 5;
    ed.lines = malloc(5 * sizeof(char*));
    ed.lines[0] = dup_cstr("Line 1");
    ed.lines[1] = dup_cstr("Line 2");
    ed.lines[2] = dup_cstr("Line 3");
    ed.lines[3] = dup_cstr("Line 4");
    ed.lines[4] = dup_cstr("Line 5");
    
    // Move lines 2-3 after line 4 (0-based: move [1,2] to after 3)
    AddressRange range = {1, 2};
    move_range(&ed, range, 3);
    
    CTEST_ASSERT_EQ(ed.num_lines, 5, "still 5 lines");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "Line 1", "line 0");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Line 4", "line 1 (was 4)");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "Line 2", "line 2 (moved)");
    CTEST_ASSERT_STR_EQ(ed.lines[3], "Line 3", "line 3 (moved)");
    CTEST_ASSERT_STR_EQ(ed.lines[4], "Line 5", "line 4");
    CTEST_ASSERT_EQ(ed.current_line, 4, "current line updated");
    
    free_editor(&ed);
}

// Test m command: move lines backward
CTEST_TEST_SIMPLE(move_command_backward) {
    Editor ed;
    init_editor(&ed);
    
    ed.num_lines = 5;
    ed.lines = malloc(5 * sizeof(char*));
    ed.lines[0] = dup_cstr("Line 1");
    ed.lines[1] = dup_cstr("Line 2");
    ed.lines[2] = dup_cstr("Line 3");
    ed.lines[3] = dup_cstr("Line 4");
    ed.lines[4] = dup_cstr("Line 5");
    
    // Move lines 4-5 after line 1 (0-based: move [3,4] to after 0)
    AddressRange range = {3, 4};
    move_range(&ed, range, 0);
    
    CTEST_ASSERT_EQ(ed.num_lines, 5, "still 5 lines");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "Line 1", "line 0");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Line 4", "line 1 (moved)");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "Line 5", "line 2 (moved)");
    CTEST_ASSERT_STR_EQ(ed.lines[3], "Line 2", "line 3");
    CTEST_ASSERT_STR_EQ(ed.lines[4], "Line 3", "line 4");
    CTEST_ASSERT_EQ(ed.current_line, 3, "current line updated");
    
    free_editor(&ed);
}

// Test t command: copy lines
CTEST_TEST_SIMPLE(copy_command) {
    Editor ed;
    init_editor(&ed);
    
    ed.num_lines = 3;
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = dup_cstr("Line 1");
    ed.lines[1] = dup_cstr("Line 2");
    ed.lines[2] = dup_cstr("Line 3");
    
    // Copy lines 1-2 after line 3 (0-based: copy [0,1] to after 2)
    AddressRange range = {0, 1};
    copy_range(&ed, range, 2);
    
    CTEST_ASSERT_EQ(ed.num_lines, 5, "5 lines after copy");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "Line 1", "line 0");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Line 2", "line 1");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "Line 3", "line 2");
    CTEST_ASSERT_STR_EQ(ed.lines[3], "Line 1", "line 3 (copy)");
    CTEST_ASSERT_STR_EQ(ed.lines[4], "Line 2", "line 4 (copy)");
    CTEST_ASSERT_EQ(ed.current_line, 4, "current line updated");
    CTEST_ASSERT_EQ(ed.dirty, 1, "dirty flag set");
    
    free_editor(&ed);
}

// Test j command: join lines
CTEST_TEST_SIMPLE(join_command_multiple_lines) {
    Editor ed;
    init_editor(&ed);
    
    ed.num_lines = 4;
    ed.lines = malloc(4 * sizeof(char*));
    ed.lines[0] = dup_cstr("Line 1");
    ed.lines[1] = dup_cstr("Hello");
    ed.lines[2] = dup_cstr(" ");
    ed.lines[3] = dup_cstr("World");
    
    // Join lines 2-4 (0-based: [1,3])
    AddressRange range = {1, 3};
    join_range(&ed, range);
    
    CTEST_ASSERT_EQ(ed.num_lines, 2, "2 lines after join");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "Line 1", "line 0 unchanged");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Hello World", "line 1 joined");
    CTEST_ASSERT_EQ(ed.current_line, 1, "current line updated");
    CTEST_ASSERT_EQ(ed.dirty, 1, "dirty flag set");
    
    free_editor(&ed);
}

// Test j command: join all lines
CTEST_TEST_SIMPLE(join_command_all_lines) {
    Editor ed;
    init_editor(&ed);
    
    ed.num_lines = 3;
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = dup_cstr("A");
    ed.lines[1] = dup_cstr("B");
    ed.lines[2] = dup_cstr("C");
    
    // Join all lines (0-based: [0,2])
    AddressRange range = {0, 2};
    join_range(&ed, range);
    
    CTEST_ASSERT_EQ(ed.num_lines, 1, "1 line after join");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "ABC", "all lines joined");
    CTEST_ASSERT_EQ(ed.current_line, 0, "current line updated");
    
    free_editor(&ed);
}

// Test move validation: can't move to within range
CTEST_TEST_SIMPLE(move_command_invalid_destination) {
    Editor ed;
    init_editor(&ed);
    
    ed.num_lines = 5;
    ed.lines = malloc(5 * sizeof(char*));
    for (int i = 0; i < 5; i++) {
        ed.lines[i] = dup_cstr("Line");
    }
    
    // Try to move lines 2-4 to within that range (invalid)
    AddressRange range = {1, 3};
    clear_last_error(&ed);
    move_range(&ed, range, 2);
    
    CTEST_ASSERT(get_last_error(&ed) != NULL, "error set");
    CTEST_ASSERT_STR_EQ(get_last_error(&ed), "Invalid destination", "correct error");
    
    free_editor(&ed);
}

// Test copy to beginning
CTEST_TEST_SIMPLE(copy_command_to_beginning) {
    Editor ed;
    init_editor(&ed);
    
    ed.num_lines = 3;
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = dup_cstr("A");
    ed.lines[1] = dup_cstr("B");
    ed.lines[2] = dup_cstr("C");
    
    // Copy line 3 to beginning (0-based: copy [2,2] to after -1, but we use 0)
    AddressRange range = {2, 2};
    copy_range(&ed, range, 0);
    
    CTEST_ASSERT_EQ(ed.num_lines, 4, "4 lines after copy");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "A", "line 0");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "C", "line 1 (copy)");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "B", "line 2");
    CTEST_ASSERT_STR_EQ(ed.lines[3], "C", "line 3");
    
    free_editor(&ed);
}

int main(void) { ctest_run_all(); return 0; }


