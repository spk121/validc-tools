#include <stdio.h>
#include <stdlib.h>
#include "bre.h"

int main(void) {
    // Test 1: Simple replacement of "foo" with "bar"
    const char *text1 = "This is foo in a string";
    const char *pattern1 = "foo";
    const char *replacement1 = "bar";

    printf("Test 1: Simple replacement\n");
    printf("Original: %s\n", text1);
    printf("Pattern: %s, Replacement: %s\n", pattern1, replacement1);

    BreMatch match1;
    if (bre_match(text1, pattern1, &match1)) {
        printf("Match found at position %d, length %d\n", match1.start, match1.length);
    } else {
        printf("No match found\n");
    }

    char *result1 = bre_substitute(text1, pattern1, replacement1);
    if (result1) {
        printf("Result: %s\n", result1);
        free(result1);
    } else {
        printf("Substitution failed\n");
    }

    // Test 2: Using capture group and backreference
    const char *text2 = "This is foo in a string";
    const char *pattern2 = "\\(foo\\)";
    const char *replacement2 = "\\1bar";

    printf("\nTest 2: Capture group and backreference\n");
    printf("Original: %s\n", text2);
    printf("Pattern: %s, Replacement: %s\n", pattern2, replacement2);

    BreMatch match2;
    if (bre_match(text2, pattern2, &match2)) {
        printf("Match found at position %d, length %d, groups %d\n", 
               match2.start, match2.length, match2.num_groups);
        for (int i = 0; i < match2.num_groups; i++) {
            printf("Group %d: start %d, length %d\n", 
                   i + 1, match2.groups[i].start, match2.groups[i].length);
        }
    } else {
        printf("No match found\n");
    }

    char *result2 = bre_substitute(text2, pattern2, replacement2);
    if (result2) {
        printf("Result: %s\n", result2);
        free(result2);
    } else {
        printf("Substitution failed\n");
    }

    return 0;
}
