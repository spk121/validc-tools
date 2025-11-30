#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ed.h"
#include "ctest.h"

// Helper to build a long string of given length with pattern 'A'..'Z'
static char *make_long_line(size_t len) {
    char *s = (char*)malloc(len + 1);
    if (!s) return NULL;
    for (size_t i = 0; i < len; ++i) s[i] = 'A' + (char)(i % 26);
    s[len] = '\0';
    return s;
}

// Test: load_file handles very long lines (> MAX_LINE)
CTEST_TEST_SIMPLE(load_file_long_line) {
    Editor ed; init_editor(&ed);
    size_t long_len = 5000; // > MAX_LINE (1024)
    char *line1 = make_long_line(long_len);
    char *line2 = make_long_line(1234);
    CTEST_ASSERT_NOT_NULL(line1, "alloc line1");
    CTEST_ASSERT_NOT_NULL(line2, "alloc line2");

    FILE *fp = fopen("test_long_load.txt", "w");
    CTEST_ASSERT_NOT_NULL(fp, "open file for write");
    fprintf(fp, "%s\n", line1);
    fprintf(fp, "%s\n", line2);
    fclose(fp);

    load_file(&ed, "test_long_load.txt");
    CTEST_ASSERT_EQ(ed.num_lines, 2, "two lines loaded");
    CTEST_ASSERT_EQ(strlen(ed.lines[0]), long_len, "length of first long line preserved");
    CTEST_ASSERT_EQ(strlen(ed.lines[1]), 1234, "length of second long line preserved");
    CTEST_ASSERT_STR_EQ(ed.lines[0], line1, "content of first long line matches");
    CTEST_ASSERT_STR_EQ(ed.lines[1], line2, "content of second long line matches");

    free(line1); free(line2);
    free_editor(&ed);
    remove("test_long_load.txt");
}

// Test: append_line handles very long user input
CTEST_TEST_SIMPLE(append_line_long_input) {
    Editor ed; init_editor(&ed);
    size_t long_len = 4096;
    char *line = make_long_line(long_len);
    CTEST_ASSERT_NOT_NULL(line, "alloc long input");

    // Prepare simulated stdin file: long line + newline + '.' + newline
    FILE *fp = fopen("test_long_append_input.tmp", "w");
    CTEST_ASSERT_NOT_NULL(fp, "open append tmp");
    fprintf(fp, "%s\n.\n", line);
    fclose(fp);

    FILE *in = fopen("test_long_append_input.tmp", "r");
    CTEST_ASSERT_NOT_NULL(in, "reopen tmp for reading");
    // Redirect stdin using freopen (portable enough for tests)
    freopen("test_long_append_input.tmp", "r", stdin);

    // Append into empty buffer: addr -1 treated as after last
    append_line(&ed, -1);

    CTEST_ASSERT_EQ(ed.num_lines, 1, "one line appended");
    CTEST_ASSERT_EQ(strlen(ed.lines[0]), long_len, "appended length preserved");
    CTEST_ASSERT_STR_EQ(ed.lines[0], line, "appended content matches");

    free(line);
    free_editor(&ed);
    fclose(in);
    remove("test_long_append_input.tmp");
}

// Test: insert_line handles long input at start
CTEST_TEST_SIMPLE(insert_line_long_input) {
    Editor ed; init_editor(&ed);
    // Seed buffer with a short line
    ed.lines = malloc(sizeof(char*));
    ed.lines[0] = strdup("Seed");
    ed.num_lines = 1;
    ed.current_line = 0;

    size_t long_len = 3000;
    char *line = make_long_line(long_len);
    CTEST_ASSERT_NOT_NULL(line, "alloc long insert line");

    FILE *fp = fopen("test_long_insert_input.tmp", "w");
    CTEST_ASSERT_NOT_NULL(fp, "open insert tmp");
    fprintf(fp, "%s\n.\n", line);
    fclose(fp);
    freopen("test_long_insert_input.tmp", "r", stdin);

    insert_line(&ed, 0); // Insert before first

    CTEST_ASSERT_EQ(ed.num_lines, 2, "two lines after insert");
    CTEST_ASSERT_EQ(strlen(ed.lines[0]), long_len, "inserted long line length");
    CTEST_ASSERT_STR_EQ(ed.lines[0], line, "inserted content matches");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "Seed", "original line shifted");

    free(line);
    free_editor(&ed);
    remove("test_long_insert_input.tmp");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    ctest_run_all();
    return 0;
}

