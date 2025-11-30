// Test file operations: e, E, r, W commands
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ed.h"
#include "ctest.h"

static char* dup_cstr(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    memcpy(p, s, n);
    return p;
}

// Test e command: edit with dirty check
CTEST_TEST_SIMPLE(edit_command_dirty_check) {
    Editor ed;
    init_editor(&ed);
    
    // Create test file
    FILE *fp = fopen("test_edit.txt", "w");
    CTEST_ASSERT(fp != NULL, "create test file");
    fprintf(fp, "Line 1\nLine 2\n");
    fclose(fp);
    
    // Load file
    load_file(&ed, "test_edit.txt");
    CTEST_ASSERT_EQ(ed.num_lines, 2, "file loaded");
    
    // Modify buffer by adding a line
    ed.num_lines = 3;
    ed.lines = realloc(ed.lines, 3 * sizeof(char*));
    ed.lines[2] = dup_cstr("Line 3");
    ed.dirty = 1;
    
    // Try to edit - should fail due to dirty flag
    clear_last_error(&ed);
    edit_file(&ed, "test_edit.txt");
    CTEST_ASSERT(get_last_error(&ed) != NULL, "error set");
    CTEST_ASSERT_STR_EQ(get_last_error(&ed), "Buffer modified", "correct error");
    
    // Still has 3 lines (edit failed)
    CTEST_ASSERT_EQ(ed.num_lines, 3, "buffer unchanged");
    
    free_editor(&ed);
    remove("test_edit.txt");
}

// Test E command: forced edit without dirty check
CTEST_TEST_SIMPLE(forced_edit_command) {
    Editor ed;
    init_editor(&ed);
    
    // Create test file
    FILE *fp = fopen("test_forced.txt", "w");
    CTEST_ASSERT(fp != NULL, "create test file");
    fprintf(fp, "Original\n");
    fclose(fp);
    
    // Load file
    load_file(&ed, "test_forced.txt");
    CTEST_ASSERT_EQ(ed.num_lines, 1, "file loaded");
    
    // Modify buffer
    ed.num_lines = 2;
    ed.lines = realloc(ed.lines, 2 * sizeof(char*));
    ed.lines[1] = dup_cstr("Modified");
    ed.dirty = 1;
    
    // Create different file
    fp = fopen("test_forced2.txt", "w");
    CTEST_ASSERT(fp != NULL, "create second file");
    fprintf(fp, "New content\n");
    fclose(fp);
    
    // Forced edit should succeed despite dirty flag
    forced_edit_file(&ed, "test_forced2.txt");
    CTEST_ASSERT_EQ(ed.num_lines, 1, "new file loaded");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "New content", "correct content");
    CTEST_ASSERT_EQ(ed.dirty, 0, "not dirty");
    
    free_editor(&ed);
    remove("test_forced.txt");
    remove("test_forced2.txt");
}

// Test r command: read file at address
CTEST_TEST_SIMPLE(read_file_command) {
    Editor ed;
    init_editor(&ed);
    
    // Create initial buffer
    ed.num_lines = 2;
    ed.lines = malloc(2 * sizeof(char*));
    ed.lines[0] = dup_cstr("Line 1");
    ed.lines[1] = dup_cstr("Line 3");
    ed.current_line = 0;
    
    // Create file to insert
    FILE *fp = fopen("test_insert.txt", "w");
    CTEST_ASSERT(fp != NULL, "create insert file");
    fprintf(fp, "Line 2\n");
    fclose(fp);
    
    // Read file after line 1 (0-based index 0)
    read_file_at_address(&ed, 0, "test_insert.txt");
    
    // Should now have 3 lines
    CTEST_ASSERT_EQ(ed.num_lines, 3, "3 lines after insert");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "Line 1", "line 0");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Line 2", "line 1");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "Line 3", "line 2");
    CTEST_ASSERT_EQ(ed.dirty, 1, "dirty flag set");
    CTEST_ASSERT_EQ(ed.current_line, 1, "current line updated");
    
    free_editor(&ed);
    remove("test_insert.txt");
}

// Test W command: write append
CTEST_TEST_SIMPLE(write_append_command) {
    Editor ed;
    init_editor(&ed);
    
    // Create initial file
    FILE *fp = fopen("test_append.txt", "w");
    CTEST_ASSERT(fp != NULL, "create append file");
    fprintf(fp, "Original line\n");
    fclose(fp);
    
    // Create buffer
    ed.num_lines = 2;
    ed.lines = malloc(2 * sizeof(char*));
    ed.lines[0] = dup_cstr("Appended 1");
    ed.lines[1] = dup_cstr("Appended 2");
    
    // Append entire buffer to file
    AddressRange range = {0, 1};
    write_append_file(&ed, range, "test_append.txt");
    
    // Verify file contents
    fp = fopen("test_append.txt", "r");
    CTEST_ASSERT(fp != NULL, "reopen file");
    char line[256];
    CTEST_ASSERT(fgets(line, sizeof(line), fp) != NULL, "read line 1");
    CTEST_ASSERT_STR_EQ(line, "Original line\n", "line 1");
    CTEST_ASSERT(fgets(line, sizeof(line), fp) != NULL, "read line 2");
    CTEST_ASSERT_STR_EQ(line, "Appended 1\n", "line 2");
    CTEST_ASSERT(fgets(line, sizeof(line), fp) != NULL, "read line 3");
    CTEST_ASSERT_STR_EQ(line, "Appended 2\n", "line 3");
    fclose(fp);
    
    free_editor(&ed);
    remove("test_append.txt");
}

// Test r command with empty buffer
CTEST_TEST_SIMPLE(read_file_empty_buffer) {
    Editor ed;
    init_editor(&ed);
    
    // Create file
    FILE *fp = fopen("test_read_empty.txt", "w");
    CTEST_ASSERT(fp != NULL, "create file");
    fprintf(fp, "First\nSecond\n");
    fclose(fp);
    
    // Read into empty buffer (addr -1 means end of buffer)
    read_file_at_address(&ed, -1, "test_read_empty.txt");
    
    CTEST_ASSERT_EQ(ed.num_lines, 2, "2 lines read");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "First", "line 0");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Second", "line 1");
    CTEST_ASSERT_EQ(ed.dirty, 1, "dirty flag set");
    
    free_editor(&ed);
    remove("test_read_empty.txt");
}

// Test e command with default filename
CTEST_TEST_SIMPLE(edit_command_default_filename) {
    Editor ed;
    init_editor(&ed);
    
    // Create test file
    FILE *fp = fopen("test_default.txt", "w");
    CTEST_ASSERT(fp != NULL, "create file");
    fprintf(fp, "Version 1\n");
    fclose(fp);
    
    // Load file (sets filename)
    load_file(&ed, "test_default.txt");
    CTEST_ASSERT_EQ(ed.num_lines, 1, "file loaded");
    CTEST_ASSERT(ed.filename != NULL, "filename set");
    
    // Update file content
    fp = fopen("test_default.txt", "w");
    fprintf(fp, "Version 2\n");
    fclose(fp);
    
    // Edit without specifying filename (should use ed.filename)
    edit_file(&ed, ed.filename);
    CTEST_ASSERT_EQ(ed.num_lines, 1, "file reloaded");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "Version 2", "updated content");
    
    free_editor(&ed);
    remove("test_default.txt");
}

int main(void) { ctest_run_all(); return 0; }


