#include <assert.h>
#include <stdio.h>
#include <string.h>

// Include the implementation to gain visibility of internal static state
#include "../ctest.c"

static int setup_called = 0;
static int func_called = 0;
static int teardown_called = 0;

static void sample_setup(CTest *ct) {
    (void)ct;
    setup_called++;
}

static void sample_func(CTest *ct) {
    (void)ct;
    func_called++;
}

static void sample_teardown(CTest *ct) {
    (void)ct;
    teardown_called++;
}

static void test_ctest_register_increments_count(void) {
    ctest_reset();
    int before = ctest_entry_count;

    CTestEntry e = {"dummy", sample_func, sample_setup, sample_teardown, false};
    ctest_register(&e);

    assert(ctest_entry_count == before + 1);
    assert(ctest_entries[ctest_entry_count - 1] == &e);
}

static void test_ctest_run_calls_setup_func_teardown(void) {
    ctest_reset();
    // Reset call counters
    setup_called = func_called = teardown_called = 0;

    // Register a fresh entry
    CTestEntry e = {"sample", sample_func, sample_setup, sample_teardown, false};
    ctest_register(&e);

    // Run all tests; should invoke setup, func, teardown for the last registered
    ctest_run_all();
    CTestSummary s = ctest_last_results();
    assert(s.tests_run == 1);
    assert(s.tests_failed == 0);
    assert(setup_called == 1);
    assert(func_called == 1);
    assert(teardown_called == 1);
}

static void failing_func(CTest *ctest) {
    // Simulate a failure by using the assertion macro
    ctest->current_test = "failing";
    CTEST_ASSERT_FALSE(1, "intentional failure");
}

static void test_ctest_counts_failures(void) {
    ctest_reset();
    // Register a failing test and a passing test
    CTestEntry pass = {"pass", sample_func, NULL, NULL, false};
    CTestEntry fail = {"fail", failing_func, NULL, NULL, false};
    ctest_register(&pass);
    ctest_register(&fail);

    func_called = 0;
    ctest_run_all();
    CTestSummary s = ctest_last_results();
    assert(s.tests_run == 2);
    assert(s.tests_failed == 1);
    assert(func_called == 1);
}

static void xfail_func(CTest *ctest) {
    ctest->current_test = "xfail";
    CTEST_ASSERT_FALSE(1, "expected failure");
}

static void test_ctest_xfail(void) {
    ctest_reset();
    CTestEntry xf = {"xfail_case", xfail_func, NULL, NULL, true};
    ctest_register(&xf);
    ctest_run_all();
    CTestSummary s = ctest_last_results();
    assert(s.tests_run == 1);
    assert(s.tests_failed == 0); // XFAIL neutralizes failure count
}

static void assert_macros_pass_func(CTest *ctest) {
    ctest->current_test = "assert_macros_pass";
    int a = 5, b = 5, c = 6;
    const char *s1 = "hello";
    const char *s2 = "hello";
    void *pnull = NULL;
    void *pnotnull = (void*)s1;

    CTEST_ASSERT_TRUE(a == b, "TRUE should pass");
    CTEST_ASSERT_FALSE(a != b, "FALSE should pass");
    CTEST_ASSERT_EQ(a, b, "EQ should pass");
    CTEST_ASSERT_NE(a, c, "NE should pass");
    CTEST_ASSERT_NULL(pnull, "NULL should pass");
    CTEST_ASSERT_NOT_NULL(pnotnull, "NOT_NULL should pass");
    CTEST_ASSERT_STR_EQ(s1, s2, "STR_EQ should pass");
}

static void assert_macros_fail_func(CTest *ctest) {
    ctest->current_test = "assert_macros_fail";
    int a = 5, b = 6;
    const char *s1 = "hello";
    const char *s2 = "world";
    void *pnull = NULL;

    // The first failure will short-circuit the test via return in macro
    CTEST_ASSERT_TRUE(a == b, "TRUE should fail");
    // The rest won't run, but that's fine — we just need one failure
    CTEST_ASSERT_EQ(a, b, "EQ should fail");
    CTEST_ASSERT_STR_EQ(s1, s2, "STR_EQ should fail");
    CTEST_ASSERT_NOT_NULL(pnull, "NOT_NULL should fail");
}

static void test_ctest_assert_macros(void) {
    ctest_reset();
    CTestEntry pass = {"assert_macros_pass", assert_macros_pass_func, NULL, NULL, false};
    CTestEntry fail = {"assert_macros_fail", assert_macros_fail_func, NULL, NULL, false};
    ctest_register(&pass);
    ctest_register(&fail);
    ctest_run_all();
    CTestSummary s = ctest_last_results();
    assert(s.tests_run == 2);
    assert(s.tests_failed == 1);
}

// Setup/teardown behaviors
static int setup_fail_called = 0;
static int func_after_setup_called = 0;
static int teardown_fail_called = 0;

static void setup_pass(CTest *ctest) {
    (void)ctest;
}
static void setup_fail(CTest *ctest) {
    setup_fail_called++;
    CTEST_ASSERT_FALSE(1, "setup failed intentionally");
}
static void func_after_setup(CTest *ctest) {
    (void)ctest;
    func_after_setup_called++;
}
static void teardown_fail(CTest *ctest) {
    teardown_fail_called++;
    CTEST_ASSERT_FALSE(1, "teardown failed intentionally");
}

static void test_ctest_setup_teardown_failures(void) {
    ctest_reset();
    setup_fail_called = 0;
    func_after_setup_called = 0;
    teardown_fail_called = 0;

    // Test where setup fails: function shouldn't run
    CTestEntry t1 = {"setup_fail_case", func_after_setup, setup_fail, NULL, false};
    // Test where teardown fails: function runs, then failure increments
    CTestEntry t2 = {"teardown_fail_case", sample_func, setup_pass, teardown_fail, false};

    ctest_register(&t1);
    ctest_register(&t2);
    ctest_run_all();
    CTestSummary s = ctest_last_results();
    assert(s.tests_run == 2);
    assert(s.tests_failed == 2); // setup failure + teardown failure
    assert(setup_fail_called == 1);
    assert(func_after_setup_called == 0);
    assert(teardown_fail_called == 1);
}

// No tests registered
static void test_ctest_no_tests(void) {
    ctest_reset();
    ctest_run_all();
    CTestSummary s = ctest_last_results();
    assert(s.tests_run == 0);
    assert(s.tests_failed == 0);
}

// XPASS scenario: test marked xfail but passes
static void xpass_func(CTest *ctest) {
    (void)ctest;
    // No failure assertions here, so it passes unexpectedly
}

static void test_ctest_xpass_counts_failure(void) {
    ctest_reset();
    CTestEntry xp = {"xpass_case", xpass_func, NULL, NULL, true};
    ctest_register(&xp);
    ctest_run_all();
    CTestSummary s = ctest_last_results();
    assert(s.tests_run == 1);
    assert(s.tests_failed == 1); // XPASS counted as unexpected failure
}

// Registry stress: try to exceed capacity
static void dummy_func(CTest *ctest) { (void)ctest; }
static void test_ctest_registry_stress(void) {
    ctest_reset();
    // Register up to capacity; current capacity is 1024
    for (int i = 0; i < 1024; ++i) {
        CTestEntry e = {"stress", dummy_func, NULL, NULL, false};
        ctest_register(&e);
    }
    // Attempt one more; it should print an error but not crash.
    CTestEntry extra = {"stress_extra", dummy_func, NULL, NULL, false};
    ctest_register(&extra);
    ctest_run_all();
    CTestSummary s = ctest_last_results();
    assert(s.tests_run == 1024);
}

// Test CTEST_TEST_SIMPLE macro
static int simple_test_ran = 0;
static void simple_test_func(CTest *ctest) {
    (void)ctest;
    simple_test_ran++;
}

static void test_ctest_simple_macro(void) {
    ctest_reset();
    simple_test_ran = 0;
    
    // Manually create an entry using the pattern CTEST_TEST_SIMPLE generates
    CTestEntry simple = {"simple_test", simple_test_func, ctest_noop_setup, ctest_noop_teardown, false};
    ctest_register(&simple);
    ctest_run_all();
    
    CTestSummary s = ctest_last_results();
    assert(s.tests_run == 1);
    assert(s.tests_failed == 0);
    assert(simple_test_ran == 1);
}

int main(void) {
    test_ctest_register_increments_count();
    test_ctest_run_calls_setup_func_teardown();
    test_ctest_counts_failures();
    test_ctest_xfail();
    test_ctest_assert_macros();
    test_ctest_setup_teardown_failures();
    test_ctest_no_tests();
    test_ctest_xpass_counts_failure();
    test_ctest_registry_stress();
    test_ctest_simple_macro();
    printf("ctest basic tests passed.\n");
    return 0;
}
