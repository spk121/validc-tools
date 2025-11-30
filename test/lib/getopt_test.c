/* getopt_test.c - TAP tests for getopt and getopt_long implementation */
#include "getopt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests = 0;
static int failed = 0;

#define OK(cond, desc) do { \
    tests++; \
    if (cond) { \
        printf("ok %d - %s\n", tests, (desc)); \
    } else { \
        printf("not ok %d - %s\n", tests, (desc)); \
        failed = 1; \
    } \
} while (0)

/* Helper to reset global getopt state */
static void reset_getopt_globals(void) {
    optind = 0; /* triggers re-initialization */
    opterr = 0; /* suppress stderr noise inside tests */
    optopt = '?';
    optarg = NULL;
}

static void test_short_basic(void) {
    char *argv[] = {"prog", "-a", "-b", "val", NULL};
    int argc = 4;
    reset_getopt_globals();
    int a_seen = 0, b_seen = 0; const char *b_arg = NULL;
    int c;
    while ((c = getopt(argc, argv, "ab:")) != -1) {
        switch (c) {
            case 'a': a_seen = 1; break;
            case 'b': b_seen = 1; b_arg = optarg; break;
            default: break;
        }
    }
    OK(a_seen, "short option -a detected");
    OK(b_seen && b_arg && strcmp(b_arg, "val") == 0, "short option -b with required arg detected");
}

static void test_short_optional_arg(void) {
    /* Provide -c (no arg) then -cfoo (attached) */
    char *argv[] = {"prog", "-c", "-cfoo", NULL};
    int argc = 3;
    reset_getopt_globals();
    int saw_plain = 0, saw_with = 0; const char *with_arg = NULL;
    int c;
    while ((c = getopt(argc, argv, "c::")) != -1) {
        if (c == 'c') {
            if (optarg) { saw_with++; with_arg = optarg; }
            else saw_plain++;
        }
    }
    OK(saw_plain == 1, "optional arg: -c without argument");
    OK(saw_with == 1 && with_arg && strcmp(with_arg, "foo") == 0, "optional arg: -cfoo attached argument");
}

static void test_short_cluster_with_required(void) {
    /* -ab: -a no argument, -b requires argument inline */
    char *argv[] = {"prog", "-abVAL", NULL};
    int argc = 2;
    reset_getopt_globals();
    int c1 = getopt(argc, argv, "ab:");
    OK(c1 == 'a', "cluster: first element -a parsed");
    int c2 = getopt(argc, argv, "ab:");
    OK(c2 == 'b' && optarg && strcmp(optarg, "VAL") == 0, "cluster: -b consumes inline argument after -a");
    int c3 = getopt(argc, argv, "ab:");
    OK(c3 == -1, "cluster: end after consuming argument");
}

static void test_unknown_option(void) {
    char *argv[] = {"prog", "-x", NULL};
    int argc = 2;
    reset_getopt_globals();
    int c = getopt(argc, argv, "ab");
    OK(c == '?', "unknown short option returns '?' ");
    OK(optopt == 'x', "optopt set to unknown option letter");
}

static void test_long_required_equals(void) {
    char *argv[] = {"prog", "--alpha=42", NULL};
    int argc = 2;
    reset_getopt_globals();
    struct option longopts[] = {
        {"alpha", required_argument, NULL, 'A'},
        {NULL, 0, NULL, 0}
    };
    int c = getopt_long(argc, argv, "", longopts, NULL);
    OK(c == 'A', "long option --alpha returns val 'A'");
    OK(optarg && strcmp(optarg, "42") == 0, "--alpha=42 captures argument");
}

static void test_long_required_space(void) {
    char *argv[] = {"prog", "--beta", "99", NULL};
    int argc = 3;
    reset_getopt_globals();
    struct option longopts[] = {
        {"beta", required_argument, NULL, 'B'},
        {NULL, 0, NULL, 0}
    };
    int c = getopt_long(argc, argv, "", longopts, NULL);
    OK(c == 'B', "long option --beta returns val 'B'");
    OK(optarg && strcmp(optarg, "99") == 0, "--beta arg separated by space captured");
}

static void test_long_optional(void) {
    char *argv[] = {"prog", "--opt", "--opt=thing", NULL};
    int argc = 3;
    reset_getopt_globals();
    struct option longopts[] = {
        {"opt", optional_argument, NULL, 'O'},
        {NULL, 0, NULL, 0}
    };
    int first = getopt_long(argc, argv, "", longopts, NULL);
    char *arg1 = optarg; /* should be NULL */
    int second = getopt_long(argc, argv, "", longopts, NULL);
    char *arg2 = optarg; /* should be thing */
    OK(first == 'O' && arg1 == NULL, "--opt without arg yields NULL optarg");
    OK(second == 'O' && arg2 && strcmp(arg2, "thing") == 0, "--opt=thing optional arg captured");
}

static void test_long_flag(void) {
    char *argv[] = {"prog", "--flag", NULL};
    int argc = 2;
    reset_getopt_globals();
    int flag_val = 0;
    struct option longopts[] = {
        {"flag", no_argument, &flag_val, 7},
        {NULL, 0, NULL, 0}
    };
    int c = getopt_long(argc, argv, "", longopts, NULL);
    OK(c == 0, "flag option returns 0 when flag pointer used");
    OK(flag_val == 7, "flag value stored via *flag");
}

static void test_ambiguous_long(void) {
    char *argv[] = {"prog", "--ver", NULL};
    int argc = 2;
    reset_getopt_globals();
    struct option longopts[] = {
        {"version", no_argument, NULL, 'v'},
        {"verbose", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0}
    };
    int c = getopt_long(argc, argv, "", longopts, NULL);
    OK(c == '?', "ambiguous prefix returns '?' ");
    OK(optopt == 0, "optopt set to 0 for ambiguous long option");
}

static void test_permutation_default(void) {
    char *argv[] = {"prog", "foo", "-a", "bar", NULL};
    int argc = 4;
    reset_getopt_globals();
    int c1 = getopt(argc, argv, "a");
    OK(c1 == 'a', "PERMUTE: -a option found, non-options skipped");
    int c2 = getopt(argc, argv, "a");
    OK(c2 == -1, "PERMUTE: end of options returns -1");
    /* After permutation, non-options move after options; optind points to first non-option */
    OK(optind == 2 && argv[optind] && strcmp(argv[optind], "foo") == 0, "PERMUTE: non-options moved to end, optind points to first");
}

static void test_permutation_interleaved(void) {
    /* Interleaved non-options and options: ensure state persists across calls */
    char *argv[] = {"prog", "-a", "file1", "-b", "file2", NULL};
    int argc = 5;
    reset_getopt_globals();
    int c1 = getopt(argc, argv, "ab");
    OK(c1 == 'a', "interleaved: first option -a");
    int c2 = getopt(argc, argv, "ab");
    OK(c2 == 'b', "interleaved: second option -b after permutation");
    /* After permutation, non-options are grouped starting at optind */
    OK(optind == 3 && strcmp(argv[optind], "file1") == 0 && strcmp(argv[optind+1], "file2") == 0, "interleaved: non-options permuted to end");
}

static void test_require_order_plus(void) {
    char *argv[] = {"prog", "foo", "-a", NULL};
    int argc = 3;
    reset_getopt_globals();
    int c1 = getopt(argc, argv, "+a");
    OK(c1 == -1, "+ leading optstring: stops at first non-option");
    OK(optind == 1, "optind remains at first non-option when REQUIRE_ORDER");
}

static void test_return_in_order_dash(void) {
    /* Leading '-' in optstring: return non-options as 1 with optarg set */
    char *argv[] = {"prog", "foo", "-a", "bar", NULL};
    int argc = 4;
    reset_getopt_globals();
    int c1 = getopt(argc, argv, "-a");
    OK(c1 == 1 && optarg && strcmp(optarg, "foo") == 0, "RETURN_IN_ORDER: non-option returned as 1 with optarg");
    int c2 = getopt(argc, argv, "-a");
    OK(c2 == 'a', "RETURN_IN_ORDER: -a parsed");
    int c3 = getopt(argc, argv, "-a");
    OK(c3 == 1 && optarg && strcmp(optarg, "bar") == 0, "RETURN_IN_ORDER: next non-option returned as 1");
    int c4 = getopt(argc, argv, "-a");
    OK(c4 == -1, "RETURN_IN_ORDER: end after all items consumed");
}

static void test_reset_optind(void) {
    char *argv[] = {"prog", "-a", NULL};
    int argc = 2;
    reset_getopt_globals();
    int c = getopt(argc, argv, "a");
    OK(c == 'a', "initial parse returns -a");
    reset_getopt_globals();
    c = getopt(argc, argv, "a");
    OK(c == 'a', "after optind reset, parse again returns -a");
}

static void test_long_only(void) {
    char *argv[] = {"prog", "-alpha", NULL};
    int argc = 2;
    reset_getopt_globals();
    struct option longopts[] = {
        {"alpha", no_argument, NULL, 'X'},
        {NULL, 0, NULL, 0}
    };
    int c = getopt_long_only(argc, argv, "", longopts, NULL);
    OK(c == 'X', "getopt_long_only parses -alpha as long option");
}

static void test_long_only_short_collision(void) {
    /* long_only should prefer long when short letter not in optstring or multi-letter follows */
    char *argv[] = {"prog", "-gamma", NULL};
    int argc = 2;
    reset_getopt_globals();
    struct option longopts[] = {
        {"gamma", no_argument, NULL, 'G'},
        {NULL, 0, NULL, 0}
    };
    int c = getopt_long_only(argc, argv, "ab", longopts, NULL);
    OK(c == 'G', "long_only: -gamma recognized as long when 'g' not in optstring");
}

static void test_missing_required_arg_short(void) {
    /* Leading ':' makes getopt return ':' on missing required arg */
    char *argv[] = {"prog", "-b", NULL};
    int argc = 2;
    reset_getopt_globals();
    int c = getopt(argc, argv, ":b:");
    OK(c == ':', "missing required arg for short option returns ':' when leading ':' present");
    OK(optopt == 'b', "optopt set to 'b' for missing arg");
}

static void test_missing_required_arg_long(void) {
    char *argv[] = {"prog", "--beta", NULL};
    int argc = 2;
    reset_getopt_globals();
    struct option longopts[] = { {"beta", required_argument, NULL, 'B'}, {NULL,0,NULL,0} };
    int c = getopt_long(argc, argv, ":", longopts, NULL);
    OK(c == ':', "missing required arg for long option returns ':' when leading ':' present");
    OK(optopt == 'B', "optopt set to long option val for missing arg");
}

static void test_W_semicolon_extension(void) {
    /* GNU extension: -W;longname translates to long option */
    char *argv[] = {"prog", "-W", "alpha", NULL};
    int argc = 3;
    reset_getopt_globals();
    struct option longopts[] = { {"alpha", no_argument, NULL, 'A'}, {NULL,0,NULL,0} };
    int c = getopt_long(argc, argv, "W;:", longopts, NULL);
    OK(c == 'A', "GNU -W;longname extension recognized");
}

int main(void) {
    /* Count of OK() calls below must match plan */
    printf("1..%d\n", 36);

    test_short_basic();             /* 2 */
    test_short_optional_arg();      /* 2 */
    test_short_cluster_with_required(); /* 3 */
    test_unknown_option();          /* 2 */
    test_long_required_equals();    /* 2 */
    test_long_required_space();     /* 2 */
    test_long_optional();           /* 2 */
    test_long_flag();               /* 2 */
    test_ambiguous_long();          /* 2 */
    test_permutation_default();     /* 3 */
    test_permutation_interleaved(); /* 3 */
    test_require_order_plus();      /* 2 */
    test_return_in_order_dash();    /* 4 */
    test_reset_optind();            /* 2 */
    test_long_only();               /* 1 */
    test_long_only_short_collision(); /* 1 */
    test_missing_required_arg_short(); /* 2 */
    test_missing_required_arg_long();  /* 2 */
    test_W_semicolon_extension();     /* 1 */

    return failed ? 1 : 0;
}
