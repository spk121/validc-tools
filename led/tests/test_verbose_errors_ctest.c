#include <stdio.h>
#include <string.h>
#include "led.h"
#include "ctest.h"

CTEST_TEST_SIMPLE(verbose_default_stores_last_error) {
    Editor ed; init_editor(&ed);
    // Trigger invalid print
    print_line(&ed, 0);
    CTEST_ASSERT_NOT_NULL(ed.last_error, "last_error set");
    CTEST_ASSERT_STR_EQ(ed.last_error, "Invalid address", "error context matches");
    CTEST_ASSERT_EQ(ed.verbose, 0, "verbose off by default");
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(verbose_toggle_changes_output) {
    Editor ed; init_editor(&ed);
    set_verbose(&ed, 1);
    CTEST_ASSERT_EQ(ed.verbose, 1, "verbose enabled");
    delete_line(&ed, 0); // invalid delete
    CTEST_ASSERT_STR_EQ(ed.last_error, "Invalid address", "delete error context");
    set_verbose(&ed, 0);
    CTEST_ASSERT_EQ(ed.verbose, 0, "verbose disabled");
    write_file(&ed, NULL); // should set no filename error
    CTEST_ASSERT_STR_EQ(ed.last_error, "No current filename", "write error context");
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(last_error_updates_each_failure) {
    Editor ed; init_editor(&ed);
    print_line(&ed, 0);
    CTEST_ASSERT_STR_EQ(ed.last_error, "Invalid address", "first error context");
    write_file(&ed, NULL);
    CTEST_ASSERT_STR_EQ(ed.last_error, "No current filename", "second error context");
    load_file(&ed, "__no_such_file__");
    CTEST_ASSERT_STR_EQ(ed.last_error, "Cannot open file", "third error context");
    free_editor(&ed);
}

int main(void) { ctest_run_all(); return 0; }

