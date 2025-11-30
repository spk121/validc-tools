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

CTEST_TEST_SIMPLE(substitute_global_simple) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(3 * sizeof(char*));
    ed.lines[0] = dup_cstr("foo");
    ed.lines[1] = dup_cstr("bar boo");
    ed.lines[2] = dup_cstr("zoo");
    ed.num_lines = 3;
    ed.current_line = 0;

    AddressRange range = {0, 2};
    substitute_range(&ed, range, "o", "0", 1);

    CTEST_ASSERT_STR_EQ(ed.lines[0], "f00", "line 1 replaced");
    CTEST_ASSERT_STR_EQ(ed.lines[1], "bar b00", "line 2 replaced");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "z00", "line 3 replaced");
    CTEST_ASSERT_EQ(ed.current_line, 2, "current line after s///g range");
    CTEST_ASSERT_EQ(ed.dirty, 1, "dirty set after substitution");

    free_editor(&ed);
}

CTEST_TEST_SIMPLE(substitute_with_backref) {
    Editor ed; init_editor(&ed);
    ed.lines = (char**)malloc(1 * sizeof(char*));
    ed.lines[0] = dup_cstr("hello");
    ed.num_lines = 1;
    ed.current_line = 0;

    AddressRange range = {0, 0};
    // h\(...\)o -> capture "ell", replace with h\1X (matches engine test)
    substitute_range(&ed, range, "h\\(...\\)o", "h\\1X", 0);

    CTEST_ASSERT_STR_EQ(ed.lines[0], "hellX", "backref substitution");
    CTEST_ASSERT_EQ(ed.current_line, 0, "current line after single-line s///");

    free_editor(&ed);
}

int main(void) { ctest_run_all(); return 0; }


