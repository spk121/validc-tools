#ifndef CTEST_H
#define CTEST_H

#include <stdbool.h>
#include <stdio.h>

// Test context
typedef struct {
    int tests_run;
    int tests_failed;
    const char *current_test;
    void *user_data; // For setup/teardown
} CTest;

// Test function type
typedef void (*CTestFunc)(CTest *);

// Setup/teardown function type
typedef void (*CTestSetupFunc)(CTest *);

// Test registration structure
typedef struct {
    const char *name;
    CTestFunc func;
    CTestSetupFunc setup;
    CTestSetupFunc teardown;
    bool xfail; // expected failure
} CTestEntry;

// Assertion macros
#define CTEST_ASSERT(test, msg) do { \
    if (!(test)) { \
        printf("FAIL: %s:%d - %s: %s\n", __FILE__, __LINE__, ctest->current_test, msg); \
        ctest->tests_failed++; \
        return; \
    } \
} while (0)

#define CTEST_ASSERT_EQ(a, b, msg) CTEST_ASSERT((a) == (b), msg)
#define CTEST_ASSERT_NE(a, b, msg) CTEST_ASSERT((a) != (b), msg)
#define CTEST_ASSERT_TRUE(cond, msg) CTEST_ASSERT((cond), msg)
#define CTEST_ASSERT_FALSE(cond, msg) CTEST_ASSERT(!(cond), msg)
#define CTEST_ASSERT_NULL(ptr, msg) CTEST_ASSERT((ptr) == NULL, msg)
#define CTEST_ASSERT_NOT_NULL(ptr, msg) CTEST_ASSERT((ptr) != NULL, msg)
#define CTEST_ASSERT_STR_EQ(s1, s2, msg) CTEST_ASSERT(strcmp((s1), (s2)) == 0, msg)

// Test definition and registration
#define CTEST_TEST(name) \
    static void name(CTest *ctest); \
    static void name##_setup(CTest *ctest) __attribute__((unused)); \
    static void name##_teardown(CTest *ctest) __attribute__((unused)); \
    static CTestEntry ctest_entry_##name = { #name, name, name##_setup, name##_teardown, false }; \
    __attribute__((constructor)) static void name##_register(void) { \
        ctest_register(&ctest_entry_##name); \
    } \
    static void name(CTest *ctest)

// Define a test that is expected to fail (XFAIL)
#define CTEST_XFAIL(name) \
    static void name(CTest *ctest); \
    static void name##_setup(CTest *ctest) __attribute__((unused)); \
    static void name##_teardown(CTest *ctest) __attribute__((unused)); \
    static CTestEntry ctest_entry_##name = { #name, name, name##_setup, name##_teardown, true }; \
    __attribute__((constructor)) static void name##_register(void) { \
        ctest_register(&ctest_entry_##name); \
    } \
    static void name(CTest *ctest)

// Test definition without setup/teardown (both are noop)
static inline void ctest_noop_setup(CTest *ctest) { (void)ctest; }
static inline void ctest_noop_teardown(CTest *ctest) { (void)ctest; }

#define CTEST_TEST_SIMPLE(name) \
    static void name(CTest *ctest); \
    static CTestEntry ctest_entry_##name = { #name, name, ctest_noop_setup, ctest_noop_teardown, false }; \
    __attribute__((constructor)) static void name##_register(void) { \
        ctest_register(&ctest_entry_##name); \
    } \
    static void name(CTest *ctest)

// Run all tests
void ctest_run_all(void);
void ctest_register(CTestEntry *entry);

// Test harness utilities
// Reset registered tests (for isolated test scenarios)
void ctest_reset(void);

// Access last run summary (tests_run and tests_failed)
typedef struct {
    int tests_run;
    int tests_failed;
} CTestSummary;

CTestSummary ctest_last_results(void);

#endif // CTEST_H
