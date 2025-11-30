// Test address range parsing and range-based commands
#include <stdlib.h>
#include <string.h>
#include "ed.h"
#include "ctest.h"

static char* dup_cstr(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    memcpy(p, s, n);
    return p;
}

CTEST_TEST_SIMPLE(parse_range_single_address) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(3 * sizeof(char*));
    ed.lines[0] = dup_cstr("A");
    ed.lines[1] = dup_cstr("B");
    ed.lines[2] = dup_cstr("C");
    ed.num_lines = 3;
    ed.current_line = 1;
    
    AddressRange range = parse_address_range(&ed, "1");
    CTEST_ASSERT_EQ(range.start, 0, "single addr start");
    CTEST_ASSERT_EQ(range.end, 0, "single addr end");
    
    range = parse_address_range(&ed, "3");
    CTEST_ASSERT_EQ(range.start, 2, "addr 3 start");
    CTEST_ASSERT_EQ(range.end, 2, "addr 3 end");
    
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(parse_range_two_addresses) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(5 * sizeof(char*));
    for (int i = 0; i < 5; i++) {
        ed.lines[i] = dup_cstr("X");
    }
    ed.num_lines = 5;
    ed.current_line = 2;
    
    AddressRange range = parse_address_range(&ed, "2,4");
    CTEST_ASSERT_EQ(range.start, 1, "range 2,4 start");
    CTEST_ASSERT_EQ(range.end, 3, "range 2,4 end");
    
    range = parse_address_range(&ed, "1,$");
    CTEST_ASSERT_EQ(range.start, 0, "range 1,$ start");
    CTEST_ASSERT_EQ(range.end, 4, "range 1,$ end");
    
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(parse_range_special_addresses) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(4 * sizeof(char*));
    for (int i = 0; i < 4; i++) {
        ed.lines[i] = dup_cstr("Y");
    }
    ed.num_lines = 4;
    ed.current_line = 1;
    
    AddressRange range = parse_address_range(&ed, ".,$");
    CTEST_ASSERT_EQ(range.start, 1, "range .,$ start");
    CTEST_ASSERT_EQ(range.end, 3, "range .,$ end");
    
    range = parse_address_range(&ed, "");
    CTEST_ASSERT_EQ(range.start, 1, "empty defaults to current start");
    CTEST_ASSERT_EQ(range.end, 1, "empty defaults to current end");
    
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(print_range_multiple_lines) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(4 * sizeof(char*));
    ed.lines[0] = dup_cstr("Line1");
    ed.lines[1] = dup_cstr("Line2");
    ed.lines[2] = dup_cstr("Line3");
    ed.lines[3] = dup_cstr("Line4");
    ed.num_lines = 4;
    ed.current_line = 0;
    
    AddressRange range = {1, 3};
    print_range(&ed, range);
    CTEST_ASSERT_EQ(ed.current_line, 3, "current line after print range");
    
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(delete_range_multiple_lines) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(5 * sizeof(char*));
    ed.lines[0] = dup_cstr("A");
    ed.lines[1] = dup_cstr("B");
    ed.lines[2] = dup_cstr("C");
    ed.lines[3] = dup_cstr("D");
    ed.lines[4] = dup_cstr("E");
    ed.num_lines = 5;
    ed.current_line = 2;
    
    AddressRange range = {1, 3}; // Delete B, C, D
    delete_range(&ed, range);
    
    CTEST_ASSERT_EQ(ed.num_lines, 2, "two lines remain");
    CTEST_ASSERT_STR_EQ(ed.lines[0], "A", "first line preserved");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "E", "last line preserved");
    CTEST_ASSERT_EQ(ed.dirty, 1, "dirty flag set");
    
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(delete_range_all_lines) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(3 * sizeof(char*));
    ed.lines[0] = dup_cstr("X");
    ed.lines[1] = dup_cstr("Y");
    ed.lines[2] = dup_cstr("Z");
    ed.num_lines = 3;
    ed.current_line = 1;
    
    AddressRange range = {0, 2};
    delete_range(&ed, range);
    
    CTEST_ASSERT_EQ(ed.num_lines, 0, "buffer empty");
    CTEST_ASSERT_EQ(ed.lines, (char**)NULL, "lines pointer null");
    CTEST_ASSERT_EQ(ed.current_line, -1, "current line reset");
    
    free_editor(&ed);
}

int main(void) { ctest_run_all(); return 0; }


