#include <stdlib.h>
#include <string.h>
#include "ed.h"
#include "ctest.h"

static char* dup_cstr(const char* s) { size_t n=strlen(s)+1; char* p=(char*)malloc(n); memcpy(p,s,n); return p; }

CTEST_TEST_SIMPLE(regex_forward_address_basic) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(4 * sizeof(char*));
    ed.lines[0]=dup_cstr("Alpha");
    ed.lines[1]=dup_cstr("Beta");
    ed.lines[2]=dup_cstr("Gamma");
    ed.lines[3]=dup_cstr("Delta");
    ed.num_lines=4; ed.current_line=1;
    int idx = parse_address(&ed, "/ta/");
    CTEST_ASSERT_EQ(idx, 3, "forward /ta/ -> Delta");
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(regex_backward_address_with_offset) {
    Editor ed; init_editor(&ed);
    ed.lines=(char**)malloc(5*sizeof(char*));
    ed.lines[0]=dup_cstr("one");
    ed.lines[1]=dup_cstr("two");
    ed.lines[2]=dup_cstr("three");
    ed.lines[3]=dup_cstr("four");
    ed.lines[4]=dup_cstr("five");
    ed.num_lines=5; ed.current_line=4; // on 'five' (0-indexed)
    int idx = parse_address(&ed, "?o?-1"); // search backward for 'o' (finds 'four' at 3), then -1 -> 'three' at 2
    CTEST_ASSERT_EQ(idx, 2, "?o?-1 -> index 2 (three)");
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(mark_and_mark_address) {
    Editor ed; init_editor(&ed);
    ed.lines=(char**)malloc(3*sizeof(char*));
    ed.lines[0]=dup_cstr("a"); ed.lines[1]=dup_cstr("b"); ed.lines[2]=dup_cstr("c");
    ed.num_lines=3; ed.current_line=2;
    // Set mark 'm' at line 2
    ed.marks['m'-'a']=1;
    int idx = parse_address(&ed, "'m");
    CTEST_ASSERT_EQ(idx, 1, "'m resolves to index 1");
    free_editor(&ed);
}

int main(void) { ctest_run_all(); return 0; }

