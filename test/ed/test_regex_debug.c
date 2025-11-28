#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../led.h"

static char* dup_cstr(const char* s) { size_t n=strlen(s)+1; char* p=(char*)malloc(n); memcpy(p,s,n); return p; }

int main(void) {
    Editor ed;
    init_editor(&ed);
    ed.lines=(char**)malloc(5*sizeof(char*));
    ed.lines[0]=dup_cstr("one");
    ed.lines[1]=dup_cstr("two");
    ed.lines[2]=dup_cstr("three");
    ed.lines[3]=dup_cstr("four");
    ed.lines[4]=dup_cstr("five");
    ed.num_lines=5;
    ed.current_line=4; // on 'five' (0-indexed)
    
    printf("Testing ?o?-1 search:\n");
    printf("Current line: %d (%s)\n", ed.current_line, ed.lines[ed.current_line]);
    
    int idx = parse_address(&ed, "?o?-1");
    printf("Result: %d\n", idx);
    if (idx >= 0 && idx < ed.num_lines) {
        printf("Found at: %s\n", ed.lines[idx]);
    } else {
        printf("Invalid result!\n");
    }
    
    // Also test just ?o? without offset
    printf("\nTesting ?o? search:\n");
    idx = parse_address(&ed, "?o?");
    printf("Result: %d\n", idx);
    if (idx >= 0 && idx < ed.num_lines) {
        printf("Found at: %s\n", ed.lines[idx]);
    }
    
    free_editor(&ed);
    return 0;
}
