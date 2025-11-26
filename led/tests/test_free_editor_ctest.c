// Validate that free_editor releases memory and resets fields.
#include <stdlib.h>
#include <string.h>
#include "led.h"
#include "ctest.h"

CTEST_TEST_SIMPLE(free_editor_resets_fields) {
    Editor ed; init_editor(&ed);
    // Populate
    // Populate editor manually
    ed.lines = (char**)malloc(sizeof(char*));
    CTEST_ASSERT_NOT_NULL(ed.lines, "alloc lines");
    ed.lines[0] = (char*)malloc(2);
    CTEST_ASSERT_NOT_NULL(ed.lines[0], "alloc line 0");
    strcpy(ed.lines[0], "X");
    ed.num_lines = 1;
    ed.current_line = 1;
    ed.dirty = 1;
    ed.filename = (char*)malloc(10);
    CTEST_ASSERT_NOT_NULL(ed.filename, "alloc filename");
    strcpy(ed.filename, "file.txt");
    set_verbose(&ed, 1);
    // Simulate error context
    // Directly set last_error through set_error for realism
    // We need a forward declaration of set_error in led.c, but here only use API effects
    // Trigger an error: invalid print
    print_line(&ed, 5); // will set last_error

    free_editor(&ed);

    CTEST_ASSERT_EQ(ed.lines, (char**)NULL, "lines reset");
    CTEST_ASSERT_EQ(ed.filename, (char*)NULL, "filename reset");
    CTEST_ASSERT_EQ(ed.last_error, (char*)NULL, "last_error reset");
    CTEST_ASSERT_EQ(ed.num_lines, 0, "num_lines reset");
    CTEST_ASSERT_EQ(ed.current_line, 0, "current_line reset");
    CTEST_ASSERT_EQ(ed.dirty, 0, "dirty reset");
    CTEST_ASSERT_EQ(ed.verbose, 0, "verbose reset");
}

int main(void) { ctest_run_all(); return 0; }
