#include "ctest.h"
#include <stdio.h>

static CTestSummary last_summary = { 0, 0 };

int ctest_run_suite(CTestEntry** suite)
{
    CTest ctest = { 0 };
    int test_count = 0;
    int unexpected_failures = 0;

    // Count tests
    for (CTestEntry** p = suite; *p; ++p) test_count++;

    printf("TAP version 14\n");
    printf("1..%d\n", test_count);

    for (CTestEntry** p = suite; *p; ++p) {
        CTestEntry* entry = *p;
        ctest.tests_run++;
        ctest.current_test = entry->name;
        int failed_before = ctest.tests_failed;

        // Run setup
        if (entry->setup) {
            entry->setup(&ctest);
        }

        // Only run test body if setup didn't fail
        if (ctest.tests_failed == failed_before) {
            entry->func(&ctest);
        }

        // Run teardown (always, even if test or setup failed)
        if (entry->teardown) {
            entry->teardown(&ctest);
        }

        bool this_failed = (ctest.tests_failed > failed_before);

        if (entry->xfail) {
            if (this_failed) {
                printf("not ok %d - %s # TODO expected failure\n",
                    ctest.tests_run, entry->name);
            }
            else {
                printf("ok %d - %s # TODO unexpected success\n",
                    ctest.tests_run, entry->name);
                unexpected_failures++;
            }
        }
        else {
            if (this_failed) {
                printf("not ok %d - %s\n", ctest.tests_run, entry->name);
                unexpected_failures++;
            }
            else {
                printf("ok %d - %s\n", ctest.tests_run, entry->name);
            }
        }
    }

    // Final summary
    if (unexpected_failures == 0) {
        printf("# All %d tests passed!\n", ctest.tests_run);
    }
    else {
        printf("# %d test(s) failed unexpectedly\n", unexpected_failures);
    }

    last_summary.tests_run = ctest.tests_run;
    last_summary.unexpected_failures = unexpected_failures;

    return unexpected_failures != 0;
}

CTestSummary ctest_last_summary(void)
{
    return last_summary;
}
