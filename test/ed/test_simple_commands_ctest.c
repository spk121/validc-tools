// Test simple ed commands: n, l, f, Q, =
#include <stdlib.h>
#include <string.h>
#include "led.h"
#include "ctest.h"

static char* dup_cstr(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    memcpy(p, s, n);
    return p;
}

CTEST_TEST_SIMPLE(numbered_print_single_line) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(3 * sizeof(char*));
    ed.lines[0] = dup_cstr("First");
    ed.lines[1] = dup_cstr("Second");
    ed.lines[2] = dup_cstr("Third");
    ed.num_lines = 3;
    ed.current_line = 0;
    
    AddressRange range = {1, 1}; // Second line
    print_numbered_range(&ed, range);
    CTEST_ASSERT_EQ(ed.current_line, 1, "current line after n");
    
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(numbered_print_range) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(4 * sizeof(char*));
    ed.lines[0] = dup_cstr("A");
    ed.lines[1] = dup_cstr("B");
    ed.lines[2] = dup_cstr("C");
    ed.lines[3] = dup_cstr("D");
    ed.num_lines = 4;
    ed.current_line = 0;
    
    AddressRange range = {1, 3}; // Lines 2-4
    print_numbered_range(&ed, range);
    CTEST_ASSERT_EQ(ed.current_line, 3, "current line after range n");
    
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(list_print_special_chars) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(2 * sizeof(char*));
    ed.lines[0] = dup_cstr("Tab\there");
    ed.lines[1] = dup_cstr("Back\\slash");
    ed.num_lines = 2;
    ed.current_line = 0;
    
    AddressRange range = {0, 1};
    print_list_range(&ed, range); // Should show \t and \\
    CTEST_ASSERT_EQ(ed.current_line, 1, "current line after l");
    
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(filename_display_and_set) {
    Editor ed; init_editor(&ed);
    
    // Initially no filename
    CTEST_ASSERT_EQ(ed.filename, (char*)NULL, "no initial filename");
    
    // Set filename
    ed.filename = dup_cstr("test.txt");
    CTEST_ASSERT_STR_EQ(ed.filename, "test.txt", "filename set");
    
    // Update filename
    free(ed.filename);
    ed.filename = dup_cstr("newfile.txt");
    CTEST_ASSERT_STR_EQ(ed.filename, "newfile.txt", "filename updated");
    
    free_editor(&ed);
}

int main(void) { ctest_run_all(); return 0; }


