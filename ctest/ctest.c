#include "ctest.h"
#include <string.h>

static CTestEntry *ctest_entries[1024]; // Max tests, adjustable
static int ctest_entry_count = 0;
static CTestSummary ctest_last_summary = {0, 0};

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
    int unexpected_failures = 0; // count failures excluding XFAILs

    for (int i = 0; i < ctest_entry_count; i++) {
        ctest.tests_run++;
        ctest.current_test = ctest_entries[i]->name;
        int failures_before_test = ctest.tests_failed;

        // Run setup if provided
        if (ctest_entries[i]->setup) {
            ctest_entries[i]->setup(&ctest);
        }

        // Only run the test function if setup did not fail
        int failures_after_setup = ctest.tests_failed;
        if (failures_after_setup == failures_before_test) {
            // setup passed; run the test body
            ctest_entries[i]->func(&ctest);
        }

        // Run teardown if provided
        if (ctest_entries[i]->teardown) {
            ctest_entries[i]->teardown(&ctest);
        }

        // Determine result considering XFAIL/XPASS across setup/body/teardown
        int failures_after_test = ctest.tests_failed;
        int this_failed = (failures_after_test > failures_before_test);
        if (ctest_entries[i]->xfail) {
            if (this_failed) {
                // Expected failure occurred
                printf("XFAIL: %s\n", ctest.current_test);
                // do not count towards unexpected failures
            } else {
                // Test unexpectedly passed
                printf("XPASS: %s\n", ctest.current_test);
                unexpected_failures += 1;
            }
        } else {
            if (this_failed) {
                unexpected_failures += 1;
            } else {
                printf("PASS: %s\n", ctest.current_test);
            }
        }
    }

    printf("\nTests run: %d, Failed: %d\n", ctest.tests_run, unexpected_failures);
    ctest_last_summary.tests_run = ctest.tests_run;
    ctest_last_summary.tests_failed = unexpected_failures;
    if (unexpected_failures == 0) {
        printf("All tests passed!\n");
    } else {
        printf("Some tests failed.\n");
    }
}

void ctest_reset(void) {
    for (int i = 0; i < ctest_entry_count; ++i) {
        ctest_entries[i] = NULL;
    }
    ctest_entry_count = 0;
    ctest_last_summary.tests_run = 0;
    ctest_last_summary.tests_failed = 0;
}

CTestSummary ctest_last_results(void) {
    return ctest_last_summary;
}
