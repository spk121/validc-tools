#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "led.h"
#include "ctest.h"

// Test: write_file with no filename and no editor filename fails
CTEST_TEST_SIMPLE(write_file_no_filename) {
    Editor ed;
    init_editor(&ed);
    
    // Manually add some lines to the buffer (bypassing stdin)
    ed.lines = malloc(2 * sizeof(char*));
    ed.lines[0] = strdup("First line");
    ed.lines[1] = strdup("Second line");
    ed.num_lines = 2;
    ed.dirty = 1;
    
    // Try to write without filename - should print "?" to stdout
    // We can't easily capture stdout, so we just verify it doesn't crash
    // and that the filename remains NULL
    write_file(&ed, NULL);
    
    CTEST_ASSERT_NULL(ed.filename, "filename should still be NULL");
    
    free_editor(&ed);
}

// Test: write_file with explicit filename
CTEST_TEST_SIMPLE(write_file_explicit_filename) {
    Editor ed;
    init_editor(&ed);
    
    // Manually add lines to the buffer
    ed.lines = malloc(3 * sizeof(char*));
    ed.lines[0] = strdup("Line 1");
    ed.lines[1] = strdup("Line 2");
    ed.lines[2] = strdup("Line 3");
    ed.num_lines = 3;
    ed.dirty = 1;
    
    // Write to explicit filename
    write_file(&ed, "test_write_output.txt");
    
    // Read the file back and verify contents
    FILE *fp = fopen("test_write_output.txt", "r");
    CTEST_ASSERT_NOT_NULL(fp, "should open output file");
    
    char line[MAX_LINE];
    fgets(line, MAX_LINE, fp);
    line[strcspn(line, "\n")] = 0;
    CTEST_ASSERT_STR_EQ(line, "Line 1", "first line should match");
    
    fgets(line, MAX_LINE, fp);
    line[strcspn(line, "\n")] = 0;
    CTEST_ASSERT_STR_EQ(line, "Line 2", "second line should match");
    
    fgets(line, MAX_LINE, fp);
    line[strcspn(line, "\n")] = 0;
    CTEST_ASSERT_STR_EQ(line, "Line 3", "third line should match");
    
    fclose(fp);
    
    // Verify editor filename was updated
    CTEST_ASSERT_NOT_NULL(ed.filename, "filename should be set");
    CTEST_ASSERT_STR_EQ(ed.filename, "test_write_output.txt", "filename should match");
    
    free_editor(&ed);
    remove("test_write_output.txt");
}

// Test: write_file uses editor filename when no argument provided
CTEST_TEST_SIMPLE(write_file_uses_editor_filename) {
    Editor ed;
    init_editor(&ed);
    
    // Load a file (this sets the filename)
    FILE *fp = fopen("test_source.txt", "w");
    fprintf(fp, "Original line 1\nOriginal line 2\n");
    fclose(fp);
    
    load_file(&ed, "test_source.txt");
    
    // Manually add a line to the buffer
    char **new_lines = realloc(ed.lines, (ed.num_lines + 1) * sizeof(char*));
    if (new_lines) {
        ed.lines = new_lines;
        ed.lines[ed.num_lines] = strdup("New line 3");
        ed.num_lines++;
        ed.dirty = 1;
    }
    
    // Write without filename - should use editor's filename
    write_file(&ed, NULL);
    
    // Read the file back and verify all three lines
    fp = fopen("test_source.txt", "r");
    CTEST_ASSERT_NOT_NULL(fp, "should open source file");
    
    char line[MAX_LINE];
    fgets(line, MAX_LINE, fp);
    line[strcspn(line, "\n")] = 0;
    CTEST_ASSERT_STR_EQ(line, "Original line 1", "first line should match");
    
    fgets(line, MAX_LINE, fp);
    line[strcspn(line, "\n")] = 0;
    CTEST_ASSERT_STR_EQ(line, "Original line 2", "second line should match");
    
    fgets(line, MAX_LINE, fp);
    line[strcspn(line, "\n")] = 0;
    CTEST_ASSERT_STR_EQ(line, "New line 3", "third line should match");
    
    fclose(fp);
    
    free_editor(&ed);
    remove("test_source.txt");
}

// Test: load_file then write_file preserves contents
CTEST_TEST_SIMPLE(load_write_roundtrip) {
    Editor ed;
    init_editor(&ed);
    
    // Create a test file with known contents
    FILE *fp = fopen("test_roundtrip.txt", "w");
    fprintf(fp, "First line of test\n");
    fprintf(fp, "Second line with special chars: @#$%%\n");
    fprintf(fp, "Third line\n");
    fprintf(fp, "Fourth line with spaces   \n");
    fclose(fp);
    
    // Load the file
    load_file(&ed, "test_roundtrip.txt");
    
    // Write it back to a different file
    write_file(&ed, "test_roundtrip_copy.txt");
    
    // Read both files and compare
    FILE *fp1 = fopen("test_roundtrip.txt", "r");
    FILE *fp2 = fopen("test_roundtrip_copy.txt", "r");
    CTEST_ASSERT_NOT_NULL(fp1, "should open original file");
    CTEST_ASSERT_NOT_NULL(fp2, "should open copy file");
    
    char line1[MAX_LINE], line2[MAX_LINE];
    int line_count = 0;
    while (fgets(line1, MAX_LINE, fp1) != NULL) {
        char *result = fgets(line2, MAX_LINE, fp2);
        CTEST_ASSERT_NOT_NULL(result, "should have matching line in copy");
        CTEST_ASSERT_STR_EQ(line1, line2, "lines should match");
        line_count++;
    }
    
    CTEST_ASSERT_EQ(line_count, 4, "should have 4 lines");
    CTEST_ASSERT_NULL(fgets(line2, MAX_LINE, fp2), "no extra lines in copy");
    
    fclose(fp1);
    fclose(fp2);
    
    free_editor(&ed);
    remove("test_roundtrip.txt");
    remove("test_roundtrip_copy.txt");
}

// Test: write to new filename updates editor filename
CTEST_TEST_SIMPLE(write_file_updates_filename) {
    Editor ed;
    init_editor(&ed);
    
    // Load a file
    FILE *fp = fopen("test_original.txt", "w");
    fprintf(fp, "Content\n");
    fclose(fp);
    load_file(&ed, "test_original.txt");
    
    CTEST_ASSERT_STR_EQ(ed.filename, "test_original.txt", "filename should be set");
    
    // Write to a new filename
    write_file(&ed, "test_newname.txt");
    
    // Verify filename was updated
    CTEST_ASSERT_STR_EQ(ed.filename, "test_newname.txt", "filename should be updated");
    
    // Manually add a line to buffer
    char **new_lines = realloc(ed.lines, (ed.num_lines + 1) * sizeof(char*));
    if (new_lines) {
        ed.lines = new_lines;
        ed.lines[ed.num_lines] = strdup("Added line");
        ed.num_lines++;
        ed.dirty = 1;
    }
    
    write_file(&ed, NULL);
    
    // Check that test_newname.txt has both lines
    fp = fopen("test_newname.txt", "r");
    char line[MAX_LINE];
    fgets(line, MAX_LINE, fp);
    line[strcspn(line, "\n")] = 0;
    CTEST_ASSERT_STR_EQ(line, "Content", "first line should match");
    fgets(line, MAX_LINE, fp);
    line[strcspn(line, "\n")] = 0;
    CTEST_ASSERT_STR_EQ(line, "Added line", "second line should match");
    fclose(fp);
    
    free_editor(&ed);
    remove("test_original.txt");
    remove("test_newname.txt");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ctest_run_all();
    return 0;
}
