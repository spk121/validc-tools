/* grep.c -- Minimal portable grep implementation using a basic regex engine
   Copyright (C) 2025  Free Software Community
   Licensed under LGPL-2.1 or later

   Notes:
   - This implementation uses a basic regular expression (BRE) engine declared in bre.h.
   - Extended regular expressions (-E/--extended-regexp) are NOT supported and will error out.
   - Tries to use only ISO C library facilities (no POSIX/Win32 specific calls).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

#include "getopt.h"
#include "bre.h"

typedef struct {
    char **items;
    size_t size;
    size_t capacity;
} StringVec;

static void vec_init(StringVec *v) {
    v->items = NULL;
    v->size = 0;
    v->capacity = 0;
}
static bool vec_push(StringVec *v, char *s) {
    if (v->size == v->capacity) {
        size_t newcap = v->capacity ? v->capacity * 2 : 8;
        char **ni = (char **)realloc(v->items, newcap * sizeof(char *));
        if (!ni) return false;
        v->items = ni;
        v->capacity = newcap;
    }
    v->items[v->size++] = s;
    return true;
}
static void vec_free(StringVec *v) {
    if (!v) return;
    for (size_t i = 0; i < v->size; ++i) free(v->items[i]);
    free(v->items);
    v->items = NULL;
    v->size = v->capacity = 0;
}

static void usage(FILE *stream, const char *progname) {
    fprintf(stream,
        "Usage: %s [-E|-F] [-i] [-v] [-w] [-x] [-c] [-l] [-n] [-q] [-s] pattern [file...]\n"
        "   or: %s [-E|-F] [-i] [-v] [-w] [-x] [-c] [-l] [-n] [-q] [-s] -e pattern ... [file...]\n"
        "   or: %s [-E|-F] [-i] [-v] [-w] [-x] [-c] [-l] [-n] [-q] [-s] -f file ... [file...]\n",
        progname, progname, progname);
}

static char *xstrdup_n(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}
static char *xstrdup(const char *s) {
    return xstrdup_n(s, strlen(s));
}

static char *tolower_dup_n(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    for (size_t i = 0; i < n; ++i) {
        unsigned char ch = (unsigned char)s[i];
        p[i] = (char)tolower(ch);
    }
    p[n] = '\0';
    return p;
}
static char *tolower_dup(const char *s) {
    return tolower_dup_n(s, strlen(s));
}

static bool is_word_char(unsigned char ch) {
    return (isalnum(ch) != 0) || ch == '_';
}

/* Read one line from stream. Grows buffer as needed.
   Returns true if a line was read; false on EOF with no data.
   The returned line is in *buf with length in *len. The line includes the
   newline character if one was read. Buffer is NUL-terminated. */
static bool read_line(FILE *f, char **buf, size_t *cap, size_t *len) {
    if (!*buf || *cap == 0) {
        *cap = 256;
        *buf = (char *)malloc(*cap);
        if (!*buf) return false;
    }
    *len = 0;
    for (;;) {
        int c = fgetc(f);
        if (c == EOF) {
            if (*len == 0) return false;
            (*buf)[*len] = '\0';
            return true;
        }
        if (*len + 1 >= *cap) {
            size_t ncap = (*cap) * 2;
            char *nb = (char *)realloc(*buf, ncap);
            if (!nb) return false;
            *buf = nb;
            *cap = ncap;
        }
        (*buf)[(*len)++] = (char)c;
        if (c == '\n') {
            (*buf)[*len] = '\0';
            return true;
        }
    }
}

/* Literal search helpers (for -F) */

static bool literal_eq_case(const char *a, size_t alen, const char *b, size_t blen, bool icase) {
    if (alen != blen) return false;
    if (!icase) return memcmp(a, b, alen) == 0;
    for (size_t i = 0; i < alen; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    }
    return true;
}

/* Find the next occurrence of needle in haystack starting from start.
   Returns index or -1 if not found. Case-insensitive if icase true. */
static long literal_find_next(const char *hay, size_t hlen, const char *ndl, size_t nlen, size_t start, bool icase) {
    if (nlen == 0) {
        if (start <= hlen) return (long)start;
        return -1;
    }
    if (nlen > hlen) return -1;
    for (size_t i = start; i + nlen <= hlen; ++i) {
        if (!icase) {
            if (memcmp(hay + i, ndl, nlen) == 0) return (long)i;
        } else {
            size_t j = 0;
            for (; j < nlen; ++j) {
                unsigned char a = (unsigned char)hay[i + j];
                unsigned char b = (unsigned char)ndl[j];
                if (tolower(a) != tolower(b)) break;
            }
            if (j == nlen) return (long)i;
        }
    }
    return -1;
}

/* Word-boundary check: ensure that the match at [start, start+len) is bounded by non-word chars or edges */
static bool boundaries_are_word(const char *text, size_t tlen, size_t start, size_t mlen) {
    if (mlen == 0) return false; /* empty string doesn't form a word */
    unsigned char left = 0, right = 0;
    bool left_ok = true, right_ok = true;
    if (start > 0) {
        left = (unsigned char)text[start - 1];
        left_ok = !is_word_char(left);
    }
    if (start + mlen < tlen) {
        right = (unsigned char)text[start + mlen];
        right_ok = !is_word_char(right);
    }
    return left_ok && right_ok;
}

/* Does a line match any of the literal patterns according to flags?
   If -x: needle must equal entire line.
   If -w: occurrence must be word-bounded.
   Returns true if match found. */
static bool line_matches_literal(const char *line, size_t llen, StringVec *patterns, bool icase, bool whole_word, bool whole_line) {
    for (size_t p = 0; p < patterns->size; ++p) {
        const char *pat = patterns->items[p];
        size_t plen = strlen(pat);
        if (whole_line) {
            if (literal_eq_case(line, llen, pat, plen, icase)) return true;
            continue;
        }
        /* search for any occurrence that meets boundary rules */
        size_t pos = 0;
        while (pos <= llen) {
            long idx = literal_find_next(line, llen, pat, plen, pos, icase);
            if (idx < 0) break;
            size_t s = (size_t)idx;
            if (!whole_word || boundaries_are_word(line, llen, s, plen)) {
                return true;
            }
            /* advance at least one char to look for another occurrence */
            pos = s + 1;
        }
    }
    return false;
}

/* Regex matching helpers */

typedef struct {
    StringVec raw;       /* original patterns */
    StringVec lower;     /* lower-cased patterns (only used if icase) */
} RegexPatterns;

static void regex_patterns_free(RegexPatterns *rp) {
    vec_free(&rp->raw);
    vec_free(&rp->lower);
}

static bool validate_regex_patterns(RegexPatterns *rp, bool icase) {
    BreMatch m;
    (void)m;
    for (size_t i = 0; i < rp->raw.size; ++i) {
        const char *pat = rp->raw.items[i];
        BreResult r = bre_match("", pat, &m);
        if (r == BRE_ERROR) return false;
        if (icase) {
            const char *lpat = rp->lower.items[i];
            r = bre_match("", lpat, &m);
            if (r == BRE_ERROR) return false;
        }
    }
    return true;
}

/* Regex search across a line. If -x: we will anchor by wrapping ^...$.
   If -w: we will iterate matches and check word boundaries. */
static bool line_matches_regex(const char *line, size_t llen, const RegexPatterns *rp, bool icase, bool whole_word, bool whole_line) {
    /* Build a temporary lower-case copy of line if icase */
    char *lower_line = NULL;
    const char *target_line = line;
    if (icase) {
        lower_line = tolower_dup_n(line, llen);
        if (!lower_line) return false; /* out-of-memory -> treat as no match */
        target_line = lower_line;
    }

    BreMatch m;
    for (size_t i = 0; i < rp->raw.size; ++i) {
        const char *pat = icase ? rp->lower.items[i] : rp->raw.items[i];

        if (whole_line) {
            /* Anchor the pattern as ^pat$ */
            size_t plen = strlen(pat);
            char *anch = (char *)malloc(plen + 3); /* ^ + pat + $ + '\0' */
            if (!anch) { free(lower_line); return false; }
            anch[0] = '^';
            memcpy(anch + 1, pat, plen);
            anch[1 + plen] = '$';
            anch[2 + plen] = '\0';
            BreResult r = bre_match(target_line, anch, &m);
            free(anch);
            if (r == BRE_OK) { free(lower_line); return true; }
            continue;
        }

        /* Not whole-line: iterate possible matches to find a word-bounded one if needed */
        size_t offset = 0;
        while (offset <= llen) {
            BreResult r = bre_match(target_line + offset, pat, &m);
            if (r == BRE_OK) {
                /* m.start and m.length are relative to target_line+offset */
                if (m.start < 0) {
                    /* safety: unexpected return; avoid infinite loop */
                    offset++;
                    continue;
                }
                size_t abs_start = offset + (size_t)m.start;
                size_t mlen = (size_t)m.length;
                if (!whole_word || boundaries_are_word(line, llen, abs_start, mlen)) {
                    free(lower_line);
                    return true;
                }
                /* advance from the match start by at least 1 to find later matches */
                offset = abs_start + (mlen > 0 ? 1 : 1);
            } else if (r == BRE_NOMATCH) {
                break;
            } else {
                /* BRE_ERROR: treat as no match for this pattern; continue others */
                break;
            }
        }
    }

    free(lower_line);
    return false;
}

/* Load patterns from a file: each line (without trailing newline) is a pattern.
   Empty lines are accepted as empty patterns (which match empty string). */
static bool load_patterns_from_file(const char *filename, StringVec *out, bool suppress_errors) {
    FILE *f = NULL;
    if (strcmp(filename, "-") == 0) {
        f = stdin;
    } else {
        f = fopen(filename, "r");
        if (!f) {
            if (!suppress_errors) {
                fprintf(stderr, "grep: %s: %s\n", filename, strerror(errno));
            }
            return false;
        }
    }

    char *buf = NULL;
    size_t cap = 0, len = 0;
    bool ok = true;
    while (read_line(f, &buf, &cap, &len)) {
        /* strip trailing newline */
        size_t plen = len;
        if (plen > 0 && buf[plen - 1] == '\n') plen--;
        char *p = xstrdup_n(buf, plen);
        if (!p) { ok = false; break; }
        if (!vec_push(out, p)) { free(p); ok = false; break; }
    }

    if (f != stdin) fclose(f);
    free(buf);
    return ok;
}

static void print_matched_line(const char *filename, bool multiple_files, bool show_lineno, long lineno, const char *line, size_t len) {
    if (multiple_files) {
        fputs(filename, stdout);
        fputc(':', stdout);
    }
    if (show_lineno) {
        fprintf(stdout, "%ld:", lineno);
    }
    /* Print the line as-is. If it doesn't end with '\n', append one to keep output tidy. */
    fwrite(line, 1, len, stdout);
    if (len == 0 || line[len - 1] != '\n') {
        fputc('\n', stdout);
    }
}

int main(int argc, char *const argv[]) {
    int c;

    /* Option flags required by POSIX */
    bool opt_E = false, opt_F = false;  /* -E extended regex (unsupported), -F fixed strings */
    bool opt_i = false;                 /* ignore case */
    bool opt_v = false;                 /* invert match */
    bool opt_w = false;                 /* match whole word */
    bool opt_x = false;                 /* match whole line */
    bool opt_c = false;                 /* count matching lines */
    bool opt_l = false;                 /* list filenames with match */
    bool opt_n = false;                 /* prefix with line number */
    bool opt_q = false;                 /* quiet, set exit status only */
    bool opt_s = false;                 /* suppress error messages about nonexistent/readable files */

    StringVec patterns; vec_init(&patterns);    /* Collected patterns (strings) */
    StringVec e_patterns; vec_init(&e_patterns);/* from -e */
    StringVec f_patterns; vec_init(&f_patterns);/* from -f files */

    const char *pattern_file_opt = NULL; /* last -f file specified */

    /* getopt_long options table */
    static const struct option longopts[] = {
        {"basic-regexp",       no_argument,       NULL, 'G'},
        {"extended-regexp",    no_argument,       NULL, 'E'},
        {"fixed-strings",      no_argument,       NULL, 'F'},
        {"ignore-case",        no_argument,       NULL, 'i'},
        {"invert-match",       no_argument,       NULL, 'v'},
        {"word-regexp",        no_argument,       NULL, 'w'},
        {"line-regexp",        no_argument,       NULL, 'x'},
        {"count",              no_argument,       NULL, 'c'},
        {"files-with-matches", no_argument,       NULL, 'l'},
        {"line-number",        no_argument,       NULL, 'n'},
        {"quiet",              no_argument,       NULL, 'q'},
        {"silent",             no_argument,       NULL, 'q'}, /* synonym */
        {"no-messages",        no_argument,       NULL, 's'},
        {"file",               required_argument, NULL, 'f'},
        {"regexp",             required_argument, NULL, 'e'},
        {"help",               no_argument,       NULL, 'H'},
        {"version",            no_argument,       NULL, 'V'},
        {NULL, 0, NULL, 0}
    };

    opterr = 1;  /* let getopt print some errors itself if any implementation does */

    while ((c = getopt_long(argc, argv, "GEFiwvxclnqse:f:HV", longopts, NULL)) != -1) {
        switch (c) {
            case 'G': /* basic (default) */ break;
            case 'E': opt_E = true; break;
            case 'F': opt_F = true; break;
            case 'i': opt_i = true; break;
            case 'v': opt_v = true; break;
            case 'w': opt_w = true; break;
            case 'x': opt_x = true; break;
            case 'c': opt_c = true; break;
            case 'l': opt_l = true; break;
            case 'n': opt_n = true; break;
            case 'q': opt_q = true; break;
            case 's': opt_s = true; break;

            case 'e': {
                char *dup = xstrdup(optarg);
                if (!dup || !vec_push(&e_patterns, dup)) {
                    if (!opt_s) fprintf(stderr, "grep: out of memory\n");
                    vec_free(&e_patterns);
                    vec_free(&f_patterns);
                    return 2;
                }
            } break;

            case 'f': {
                pattern_file_opt = optarg; /* track last -f for messaging; we support multiple by loading immediately */
                if (!load_patterns_from_file(optarg, &f_patterns, opt_s)) {
                    /* load function prints its own error (unless -s), but we must signal failure exit status 2 */
                    vec_free(&e_patterns);
                    vec_free(&f_patterns);
                    return 2;
                }
            } break;

            case 'H':
                usage(stdout, argv[0]);
                vec_free(&e_patterns);
                vec_free(&f_patterns);
                return EXIT_SUCCESS;

            case 'V':
                printf("grep (minimal portable) 2025\n");
                vec_free(&e_patterns);
                vec_free(&f_patterns);
                return EXIT_SUCCESS;

            case '?':
            default:
                /* getopt prints an error for unknown options */
                vec_free(&e_patterns);
                vec_free(&f_patterns);
                return 2;
        }
    }

    /* Error on -E: Only basic regular expressions supported */
    if (opt_E) {
        if (!opt_s) {
            fprintf(stderr, "grep: -E/--extended-regexp is not supported; only basic regular expressions (-G/--basic-regexp) are implemented.\n");
        }
        vec_free(&e_patterns);
        vec_free(&f_patterns);
        return 2;
    }

    /* If both -E and -F were given, -E already errored out; but if both -G and -F it's fine (just -F). */

    /* Collect positional pattern if no -e and no -f patterns were provided after options */
    bool have_any_pattern = (e_patterns.size > 0) || (f_patterns.size > 0);
    if (!have_any_pattern) {
        if (optind >= argc) {
            if (!opt_s) {
                fprintf(stderr, "%s: missing pattern\n", argv[0]);
                usage(stderr, argv[0]);
            }
            vec_free(&e_patterns);
            vec_free(&f_patterns);
            return 2;
        }
        /* First non-option is the pattern */
        char *dup = xstrdup(argv[optind++]);
        if (!dup || !vec_push(&patterns, dup)) {
            if (!opt_s) fprintf(stderr, "grep: out of memory\n");
            vec_free(&patterns);
            vec_free(&e_patterns);
            vec_free(&f_patterns);
            return 2;
        }
    } else {
        /* Merge -e and -f patterns (all of them) */
        for (size_t i = 0; i < e_patterns.size; ++i) {
            char *dup = xstrdup(e_patterns.items[i]);
            if (!dup || !vec_push(&patterns, dup)) {
                if (!opt_s) fprintf(stderr, "grep: out of memory\n");
                vec_free(&patterns);
                vec_free(&e_patterns);
                vec_free(&f_patterns);
                return 2;
            }
        }
        for (size_t i = 0; i < f_patterns.size; ++i) {
            char *dup = xstrdup(f_patterns.items[i]);
            if (!dup || !vec_push(&patterns, dup)) {
                if (!opt_s) fprintf(stderr, "grep: out of memory\n");
                vec_free(&patterns);
                vec_free(&e_patterns);
                vec_free(&f_patterns);
                return 2;
            }
        }
        /* optind points to first filename now */
    }
    vec_free(&e_patterns);
    vec_free(&f_patterns);

    if (patterns.size == 0) {
        if (!opt_s) fprintf(stderr, "grep: no pattern supplied\n");
        vec_free(&patterns);
        return 2;
    }

    /* Prepare regex patterns structure if needed */
    RegexPatterns rp;
    vec_init(&rp.raw);
    vec_init(&rp.lower);

    if (!opt_F) {
        /* Move ownership of patterns into rp.raw, and build lower case copies if -i */
        for (size_t i = 0; i < patterns.size; ++i) {
            if (!vec_push(&rp.raw, patterns.items[i])) {
                if (!opt_s) fprintf(stderr, "grep: out of memory\n");
                regex_patterns_free(&rp);
                /* Avoid double free of items we've moved. Free remaining unmoved. */
                for (size_t j = i + 1; j < patterns.size; ++j) free(patterns.items[j]);
                free(patterns.items);
                return 2;
            }
            patterns.items[i] = NULL; /* moved */
            if (opt_i) {
                char *lp = tolower_dup(rp.raw.items[i]);
                if (!lp || !vec_push(&rp.lower, lp)) {
                    if (!opt_s) fprintf(stderr, "grep: out of memory\n");
                    regex_patterns_free(&rp);
                    for (size_t j = i + 1; j < patterns.size; ++j) free(patterns.items[j]);
                    free(patterns.items);
                    return 2;
                }
            }
        }
        free(patterns.items); /* only container remains */
        patterns.items = NULL;
        patterns.size = patterns.capacity = 0;

        /* Validate regex syntax now to fail early */
        if (!validate_regex_patterns(&rp, opt_i)) {
            if (!opt_s) {
                fprintf(stderr, "grep: invalid basic regular expression");
                if (pattern_file_opt) fprintf(stderr, " (from %s)", pattern_file_opt);
                fputc('\n', stderr);
            }
            regex_patterns_free(&rp);
            return 2;
        }
    }

    /* Remaining arguments are filenames (or stdin if none) */
    const char **files = (const char **)&argv[optind];
    int file_count = argc - optind;

    bool multiple_files = file_count > 1 || (file_count == 1 && strcmp(files[0], "-") != 0 ? false : false);
    /* For grep, when more than one file is processed, prefix filename. We'll set multiple_files if file_count > 1. */
    multiple_files = file_count > 1;

    bool any_match = false;
    bool had_error = false;

    /* Helper lambda-like to process a stream */
    struct Processor {
        /* state captured by functions below */
    } proc;

    for (int fi = 0; fi < (file_count == 0 ? 1 : file_count); ++fi) {
        const char *fname = (file_count == 0) ? "-" : files[fi];
        FILE *f = NULL;
        if (strcmp(fname, "-") == 0) {
            f = stdin;
        } else {
            f = fopen(fname, "r");
            if (!f) {
                if (!opt_s) fprintf(stderr, "grep: %s: %s\n", fname, strerror(errno));
                had_error = true;
                continue;
            }
        }

        char *line = NULL;
        size_t cap = 0, len = 0;
        long lineno = 0;
        size_t match_count = 0;
        bool printed_filename_for_l = false;
        while (read_line(f, &line, &cap, &len)) {
            lineno++;
            /* Exclude trailing newline from matching; print will use it as-is */
            size_t content_len = len;
            if (content_len > 0 && line[content_len - 1] == '\n') content_len--;

            bool matched = false;
            if (opt_F) {
                matched = line_matches_literal(line, content_len, &patterns, opt_i, opt_w, opt_x);
            } else {
                matched = line_matches_regex(line, content_len, &rp, opt_i, opt_w, opt_x);
            }
            if (opt_v) matched = !matched;

            if (matched) {
                any_match = true;
                if (opt_q) {
                    /* Quiet: exit immediately success */
                    free(line);
                    if (f != stdin) fclose(f);
                    if (!opt_F) regex_patterns_free(&rp);
                    else vec_free(&patterns);
                    return 0;
                }
                if (opt_l) {
                    if (!printed_filename_for_l) {
                        fputs(fname, stdout);
                        fputc('\n', stdout);
                        printed_filename_for_l = true;
                    }
                    /* Continue reading to next file; no need to read remaining lines if -l behaves as first match only.
                       But POSIX allows listing file name once; we can break early. */
                    break; /* list filename once and stop scanning this file */
                } else if (opt_c) {
                    match_count++;
                } else {
                    print_matched_line(fname, multiple_files, opt_n, lineno, line, len);
                }
            }
        }

        if (opt_l) {
            /* already printed file name if matched; else nothing */
        } else if (opt_c) {
            if (multiple_files) {
                printf("%s:%zu\n", fname, match_count);
            } else {
                printf("%zu\n", match_count);
            }
        }

        free(line);
        if (f != stdin) fclose(f);
    }

    if (!opt_F) regex_patterns_free(&rp);
    else vec_free(&patterns);

    if (had_error) return 2;
    return any_match ? 0 : 1;
}
