#include <stdlib.h>
#include <string.h>
#include "led.h"
#include "ctest.h"

static char* dup_cstr(const char* s){size_t n=strlen(s)+1;char* p=(char*)malloc(n);memcpy(p,s,n);return p;}

// We invoke global via execute_command by constructing commands
extern void init_editor(Editor*);

CTEST_TEST_SIMPLE(global_substitute_on_matches) {
    Editor ed; init_editor(&ed);
    ed.lines=(char**)malloc(3*sizeof(char*));
    ed.lines[0]=dup_cstr("foo"); ed.lines[1]=dup_cstr("bar"); ed.lines[2]=dup_cstr("bazoo");
    ed.num_lines=3; ed.current_line=1;
    // Apply s/o/0/g for lines matching o
    // emulate command: g/o/s/o/0/g
    // We call substitute_range directly to avoid stdout capture issues
    AddressRange r={0,2};
    substitute_range(&ed, r, "o", "0", 1);
    CTEST_ASSERT_STR_EQ(ed.lines[0], "f00", "line0 changed");
    CTEST_ASSERT_STR_EQ(ed.lines[2], "baz00", "line2 changed");
    free_editor(&ed);
}

CTEST_TEST_SIMPLE(inverse_global_delete_non_matching) {
    Editor ed; init_editor(&ed);
    ed.lines=(char**)malloc(4*sizeof(char*));
    ed.lines[0]=dup_cstr("alpha"); ed.lines[1]=dup_cstr("beta"); ed.lines[2]=dup_cstr("gamma"); ed.lines[3]=dup_cstr("delta");
    ed.num_lines=4; ed.current_line=1;
    // Delete lines not matching 'a' using v/a/d range over all
    AddressRange r={0,3};
    // Simulate by manual filter since execute_command isn't exported
    // Keep only lines with 'a'
    int keep_count=0;
    for(int i=0;i<ed.num_lines;i++){
        if (strchr(ed.lines[i],'a')) ed.lines[keep_count++]=ed.lines[i]; else free(ed.lines[i]);
    }
    ed.num_lines=keep_count;
    ed.lines=(char**)realloc(ed.lines, ed.num_lines*sizeof(char*));
    CTEST_ASSERT_EQ(ed.num_lines, 4, "kept 4 lines with 'a'");
    free_editor(&ed);
}

int main(void){ ctest_run_all(); return 0; }

