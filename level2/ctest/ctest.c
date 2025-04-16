#include "ctest.h"
#include <string.h>

static CTestEntry *ctest_entries[1024]; // Max tests, adjustable
static int ctest_entry_count = 0;

void ctest_register(CTestEntry *entry) {
    if (ctest_entry_count < 1024) {
        ctest_entries[ctest_entry_count++] = entry;
    } else {
        printf("Error: Too many tests registered (max %d)\n", 1024);
    }
}

void ctest_run_all(void) {
    CTest ctest = {0, 0, NULL, NULL};
    printf("Running %d tests...\n", ctest_entry_count);

    for (int i = 0; i < ctest_entry_count; i++) {
        ctest.tests_run++;
        ctest.current_test = ctest_entries[i]->name;

        // Run setup if provided
        if (ctest_entries[i]->setup) {
            ctest_entries[i]->setup(&ctest);
        }

        ctest_entries[i]->func(&ctest);

        // Run teardown if provided
        if (ctest_entries[i]->teardown) {
            ctest_entries[i]->teardown(&ctest);
        }

        if (ctest.tests_failed == 0 || ctest.tests_failed < ctest.tests_run) {
            printf("PASS: %s\n", ctest.current_test);
        }
    }

    printf("\nTests run: %d, Failed: %d\n", ctest.tests_run, ctest.tests_failed);
    if (ctest.tests_failed == 0) {
        printf("All tests passed!\n");
    } else {
        printf("Some tests failed.\n");
    }
}
