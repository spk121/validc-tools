/* getopt.h -- Portable GNU-like getopt + getopt_long for ISO C23
   Copyright (C) 1987-2025 Free Software Foundation, Inc.
   Modified 2025 to be pure ISO C23 (no POSIX environment, no Win32).

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, version 2.1 or later.
*/

#ifndef GETOPT_H
#define GETOPT_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* Argument types for long options */
    enum
    {
        no_argument = 0,
        required_argument = 1,
        optional_argument = 2
    };

    struct option
    {
        const char *name; /* long option name */
        int has_arg;      /* no_argument, required_argument, optional_argument */
        int *flag;        /* if non-NULL, store val here and return 0 */
        int val;          /* value to return or store in *flag */
    };

    /* Traditional external variables */
    extern char *optarg;
    extern int optind;
    extern int opterr;
    extern int optopt;

    /* Short-option interface */
    int getopt(int argc, char *const argv[], const char *optstring);

    /* Long option interfaces */
    int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts, int *longind);

    int getopt_long_only(int argc, char *const argv[], const char *optstring, const struct option *longopts,
                         int *longind);

    /* Re-entrant state and entry points */
    typedef enum
    {
        REQUIRE_ORDER,
        PERMUTE,
        RETURN_IN_ORDER
    } getopt_ordering_t;

    struct getopt_state
    {
        /* Publicly visible fields mirror traditional globals */
        int optind;
        int opterr;
        int optopt;
        char *optarg;

        /* Internal fields */
        int initialized;
        char *__nextchar;
        getopt_ordering_t ordering;
        int first_nonopt;
        int last_nonopt;
    };

    int getopt_r(int argc, char *const argv[], const char *optstring, struct getopt_state *state);

    int getopt_long_r(int argc, char *const argv[], const char *optstring, const struct option *longopts, int *longind,
                      struct getopt_state *state);

    int getopt_long_only_r(int argc, char *const argv[], const char *optstring, const struct option *longopts,
                           int *longind, struct getopt_state *state);

    /* Internal shared core (ISO C, no POSIX).
       posixly_correct: 0 for GNU permutation default, 1 to require ordering. */
    int _getopt_internal_r(int argc, char *const argv[], const char *optstring, const struct option *longopts,
                           int *longind, int long_only, int posixly_correct, struct getopt_state *state);

#ifdef __cplusplus
}
#endif

#endif /* GETOPT_H */
