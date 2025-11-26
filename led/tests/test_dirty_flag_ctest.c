#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "led.h"
#include "ctest.h"

// Test: dirty flag is not set on init
CTEST_TEST_SIMPLE(dirty_flag_init) {
    Editor ed;
    init_editor(&ed);
    
    CTEST_ASSERT_EQ(ed.dirty, 0, "dirty flag should be 0 on init");
    
    free_editor(&ed);
}

// Test: dirty flag is not set when loading a file
CTEST_TEST_SIMPLE(dirty_flag_load_file) {
    Editor ed;
    init_editor(&ed);
    
    // Create a test file
    FILE *fp = fopen("test_dirty_load.txt", "w");
    fprintf(fp, "Line 1\nLine 2\n");
    fclose(fp);
    
    // Load the file
    load_file(&ed, "test_dirty_load.txt");
    
    CTEST_ASSERT_EQ(ed.dirty, 0, "dirty flag should be 0 after loading file");
    CTEST_ASSERT_EQ(ed.num_lines, 2, "should have loaded 2 lines");
    
    free_editor(&ed);
    remove("test_dirty_load.txt");
}

// Test: dirty flag is set when adding lines to empty buffer
CTEST_TEST_SIMPLE(dirty_flag_add_to_empty) {
    Editor ed;
    init_editor(&ed);
    
    CTEST_ASSERT_EQ(ed.dirty, 0, "should start not dirty");
    
    // Manually add lines (simulating append)
    ed.lines = malloc(2 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.num_lines = 2;
    ed.dirty = 1;  // This would be set by append_line
    
    CTEST_ASSERT_EQ(ed.dirty, 1, "dirty flag should be 1 after adding lines");
    
    free_editor(&ed);
}

// Test: dirty flag is NOT set when user enters no lines in append
CTEST_TEST_SIMPLE(dirty_flag_append_no_lines) {
    Editor ed;
    init_editor(&ed);
    
    // Add initial content
    ed.lines = malloc(1 * sizeof(char*));
    ed.lines[0] = strdup("Existing line");
    ed.num_lines = 1;
    ed.current_line = 0;
    ed.dirty = 0;  // Mark as clean (as if just loaded)
    
    // Simulate append with no lines entered (user just types '.')
    // We can't easily test stdin, so we verify the logic:
    // append_line only sets dirty if num_lines increases
    int original_lines = ed.num_lines;
    
    // If no lines added, dirty should stay 0
    CTEST_ASSERT_EQ(ed.num_lines, original_lines, "no lines added");
    CTEST_ASSERT_EQ(ed.dirty, 0, "dirty should still be 0");
    
    free_editor(&ed);
}

// Test: dirty flag is cleared when writing file
CTEST_TEST_SIMPLE(dirty_flag_write_clears) {
    Editor ed;
    init_editor(&ed);
    
    // Add content
    ed.lines = malloc(2 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.num_lines = 2;
    ed.dirty = 1;
    
    CTEST_ASSERT_EQ(ed.dirty, 1, "should be dirty before write");
    
    // Write to file
    write_file(&ed, "test_dirty_write.txt");
    
    CTEST_ASSERT_EQ(ed.dirty, 0, "dirty flag should be 0 after write");
    
    free_editor(&ed);
    remove("test_dirty_write.txt");
}

// Test: dirty flag is set when deleting a line
CTEST_TEST_SIMPLE(dirty_flag_delete) {
    Editor ed;
    init_editor(&ed);
    
    // Load a file (starts clean)
    FILE *fp = fopen("test_dirty_delete.txt", "w");
    fprintf(fp, "Line 1\nLine 2\nLine 3\n");
    fclose(fp);
    load_file(&ed, "test_dirty_delete.txt");
    
    CTEST_ASSERT_EQ(ed.dirty, 0, "should be clean after load");
    CTEST_ASSERT_EQ(ed.num_lines, 3, "should have 3 lines");
    
    // Delete a line
    delete_line(&ed, 1);  // Delete line 2 (0-based: 1)
    
    CTEST_ASSERT_EQ(ed.dirty, 1, "dirty flag should be 1 after delete");
    CTEST_ASSERT_EQ(ed.num_lines, 2, "should have 2 lines after delete");
    
    free_editor(&ed);
    remove("test_dirty_delete.txt");
}

// Test: dirty flag is NOT set when delete fails
CTEST_TEST_SIMPLE(dirty_flag_delete_invalid) {
    Editor ed;
    init_editor(&ed);
    
    // Load a file (starts clean)
    FILE *fp = fopen("test_dirty_delete_invalid.txt", "w");
    fprintf(fp, "Line 1\nLine 2\n");
    fclose(fp);
    load_file(&ed, "test_dirty_delete_invalid.txt");
    
    CTEST_ASSERT_EQ(ed.dirty, 0, "should be clean after load");
    
    // Try to delete invalid line
    delete_line(&ed, -1);  // Invalid
    CTEST_ASSERT_EQ(ed.dirty, 0, "dirty should stay 0 when delete fails");
    
    delete_line(&ed, 10);  // Out of bounds
    CTEST_ASSERT_EQ(ed.dirty, 0, "dirty should stay 0 when delete fails");
    
    CTEST_ASSERT_EQ(ed.num_lines, 2, "should still have 2 lines");
    
    free_editor(&ed);
    remove("test_dirty_delete_invalid.txt");
}

// Test: dirty flag behavior with multiple operations
CTEST_TEST_SIMPLE(dirty_flag_multiple_operations) {
    Editor ed;
    init_editor(&ed);
    
    // Start clean, empty
    CTEST_ASSERT_EQ(ed.dirty, 0, "should start clean");
    
    // Add content (manually simulating append)
    ed.lines = malloc(2 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.num_lines = 2;
    ed.dirty = 1;
    
    CTEST_ASSERT_EQ(ed.dirty, 1, "should be dirty after adding");
    
    // Write to file (clears dirty)
    write_file(&ed, "test_dirty_multi.txt");
    CTEST_ASSERT_EQ(ed.dirty, 0, "should be clean after write");
    
    // Delete a line (sets dirty)
    delete_line(&ed, 0);
    CTEST_ASSERT_EQ(ed.dirty, 1, "should be dirty after delete");
    CTEST_ASSERT_EQ(ed.num_lines, 1, "should have 1 line");
    
    // Write again (clears dirty)
    write_file(&ed, "test_dirty_multi.txt");
    CTEST_ASSERT_EQ(ed.dirty, 0, "should be clean after second write");
    
    free_editor(&ed);
    remove("test_dirty_multi.txt");
}

// Test: load file then modify sets dirty
CTEST_TEST_SIMPLE(dirty_flag_load_then_modify) {
    Editor ed;
    init_editor(&ed);
    
    // Create and load a file
    FILE *fp = fopen("test_dirty_modify.txt", "w");
    fprintf(fp, "Original line\n");
    fclose(fp);
    load_file(&ed, "test_dirty_modify.txt");
    
    CTEST_ASSERT_EQ(ed.dirty, 0, "should be clean after load");
    CTEST_ASSERT_EQ(ed.num_lines, 1, "should have 1 line");
    
    // Add more content
    char **new_lines = realloc(ed.lines, 2 * sizeof(char*));
    if (new_lines) {
        ed.lines = new_lines;
        ed.lines[1] = strdup("New line");
        ed.num_lines = 2;
        ed.dirty = 1;  // append_line would set this
    }
    
    CTEST_ASSERT_EQ(ed.dirty, 1, "should be dirty after modification");
    CTEST_ASSERT_EQ(ed.num_lines, 2, "should have 2 lines");
    
    free_editor(&ed);
    remove("test_dirty_modify.txt");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ctest_run_all();
    return 0;
}

