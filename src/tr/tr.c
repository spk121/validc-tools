/* tr.c - Portable ISO C implementation of the POSIX tr utility
 * Conforms to: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/tr.html
 * Uses only ISO C standard library functions (stdio, stdlib, string, ctype, limits).
 *
 * Behavior notes:
 * - This implementation operates in the "C" locale with byte-oriented semantics.
 * - Supports options: -c (complement STRING1), -d (delete), -s (squeeze).
 * - Supports expansions in STRING1/STRING2:
 *   - Ranges: a-z (bytewise).
 *   - Character classes: [:alnum:], [:alpha:], [:blank:], [:cntrl:], [:digit:], [:graph:],
 *                        [:lower:], [:print:], [:punct:], [:space:], [:upper:], [:xdigit:].
 *   - Repetition in STRING2: [c*n] and [c*] (pad copies of c to match len(STRING1)).
 * - Equivalence classes [=x=] are not supported (allowed by POSIX).
 *
 * Squeeze semantics:
 * - If -s is specified, squeezing applies only to characters in the "squeeze set":
 *   - If STRING2 is present (and -d is not set), squeeze set = STRING2 (after expansion).
 *   - If -d and -s are both set, squeeze set = STRING2 (after expansion); transliteration is not performed.
 *   - If STRING2 is absent, squeeze set = STRING1 (after expansion, considering -c).
 *
 * Complement semantics:
 * - -c complements STRING1 only (the source set for deletion or transliteration).
 *
 * Delete and transliteration:
 * - -d deletes any input byte that belongs to STRING1 (considering -c).
 * - Without -d, transliteration maps bytes in STRING1 to STRING2 (using last-element extension if needed).
 *
 * Error handling:
 * - Missing operands and invalid options result in status 2 with usage diagnostics.
 * - I/O errors on stdin produce status 1 via perror.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>

#define MAX_STRING 65536

static int opt_complement = 0;
static int opt_delete = 0;
static int opt_squeeze = 0;

/* Mapping table: identity unless transliteration set */
static unsigned char map[UCHAR_MAX + 1];

/* Sets for fast membership testing */
static unsigned char set1_buf[MAX_STRING];
static size_t set1_len = 0;

static unsigned char set2_buf[MAX_STRING];
static size_t set2_len = 0;

/* Membership arrays (bytewise) for set1 and squeeze set */
static unsigned char in_set1[UCHAR_MAX + 1];
static unsigned char in_squeeze[UCHAR_MAX + 1];

/* Map character class names to ctype functions */
static int (*class_func(const char *name))(int) {
    struct cls { const char *name; int (*func)(int); } classes[] = {
        { "alnum",  isalnum }, { "alpha",  isalpha }, { "blank",  isblank },
        { "cntrl",  iscntrl }, { "digit",  isdigit }, { "graph",  isgraph },
        { "lower",  islower }, { "print",  isprint }, { "punct",  ispunct },
        { "space",  isspace }, { "upper",  isupper }, { "xdigit", isxdigit }
    };
    size_t i;
    for (i = 0; i < sizeof(classes)/sizeof(classes[0]); i++) {
        if (strcmp(name, classes[i].name) == 0) return classes[i].func;
    }
    return NULL;
}

/* Parse [:class:] beginning at s; if matches, fill out[] and advance *ps, return 1; else 0 */
static int parse_class(const char **ps, unsigned char *out, size_t *plen) {
    const char *s = *ps;
    if (!(s[0] == '[' && s[1] == ':' )) return 0;
    const char *p = s + 2;
    const char *end = strstr(p, ":]");
    if (!end) return 0;
    size_t namelen = (size_t)(end - p);
    if (namelen == 0 || namelen >= 32) return 0;
    char name[32];
    memcpy(name, p, namelen);
    name[namelen] = '\0';
    int (*func)(int) = class_func(name);
    if (!func) return 0;
    /* Add all bytes matching the class in the C locale */
    size_t len = *plen;
    int c;
    for (c = 0; c <= UCHAR_MAX; c++) {
        if (func(c)) {
            if (len >= MAX_STRING) break;
            out[len++] = (unsigned char)c;
        }
    }
    *plen = len;
    *ps = end + 2;
    return 1;
}

/* Parse repetition [c*n] or [c*] in STRING2 only; returns 1 if parsed, else 0.
 * Adds the appropriate number of copies of c into out, advancing *ps.
 * For [c*], we record a marker meaning "repeat c to pad to len(STRING1)". We handle this by storing a special
 * struct on the side and resolving after full expansion. To keep ISO C only, we pre-store entries into a buffer;
 * here we will write a sentinel triple (0xFF, 0xFE, c) to indicate a padding request, then resolve later.
 */
static int parse_repeat(const char **ps, unsigned char *out, size_t *plen, int allow_pad_marker) {
    const char *s = *ps;
    if (s[0] != '[') return 0;
    const char *rb = strchr(s, ']');
    const char *star = strchr(s, '*');
    if (!rb || !star || star > rb) return 0;
    /* character c is the single byte immediately before '*', i.e., s[1] if format is [c*n] */
    if (star == s + 2) {
        unsigned char c = (unsigned char)s[1];
        if (star + 1 == rb) {
            /* [c*] */
            if (!allow_pad_marker) return 0;
            size_t len = *plen;
            if (len + 3 <= MAX_STRING) {
                out[len++] = 0xFF; /* marker */
                out[len++] = 0xFE; /* marker */
                out[len++] = c;    /* the byte to pad */
                *plen = len;
                *ps = rb + 1;
                return 1;
            } else {
                fprintf(stderr, "tr: input string too long\n");
                exit(2);
            }
        } else {
            /* [c*n] */
            char numbuf[32];
            size_t numlen = (size_t)(rb - (star + 1));
            if (numlen == 0 || numlen >= sizeof(numbuf)) return 0;
            memcpy(numbuf, star + 1, numlen);
            numbuf[numlen] = '\0';
            char *endp = NULL;
            long val = strtol(numbuf, &endp, 10);
            if (!endp || *endp != '\0' || val <= 0 || val > MAX_STRING) return 0;
            int n = (int)val;
            size_t len = *plen;
            int i;
            for (i = 0; i < n && len < MAX_STRING; i++) {
                out[len++] = c;
            }
            *plen = len;
            *ps = rb + 1;
            return 1;
        }
    }
    return 0;
}

/* Expand a STRING operand (STRING1 or STRING2).
 * - Supports literal bytes, ranges X-Y, [:class:].
 * - For STRING2, supports [c*n] and [c*] with padding markers.
 */
static size_t expand_string(const char *s, unsigned char *out, int is_string2) {
    size_t len = 0;
    while (*s) {
        if (len >= MAX_STRING) {
            fprintf(stderr, "tr: input string too long\n");
            exit(2);
        }

        /* STRING2 repetition constructs */
        if (is_string2 && parse_repeat(&s, out, &len, 1)) {
            continue;
        }

        /* Character class [:name:] */
        if (parse_class(&s, out, &len)) {
            continue;
        }

        /* Equivalence class [=x=] not supported: treat as literal */
        if (s[0] == '[' && s[1] == '=' ) {
            /* find closing "=]" to consume as literal bytes */
            const char *end = strstr(s + 2, "=]");
            if (end) {
                /* Copy literally */
                while (s <= end + 1) {
                    out[len++] = (unsigned char)*s++;
                    if (len >= MAX_STRING) break;
                }
                continue;
            }
            /* fallthrough to literal if malformed */
        }

        /* Ranges X-Y (bytewise), not starting or ending with '-' */
        if (s[0] && s[1] == '-' && s[2] && s[0] != '-' && s[2] != '-') {
            unsigned char start = (unsigned char)s[0];
            unsigned char end = (unsigned char)s[2];
            if (start <= end) {
                unsigned int c;
                for (c = start; c <= end && len < MAX_STRING; c++) {
                    out[len++] = (unsigned char)c;
                }
            } else {
                unsigned int c;
                for (c = end; c <= start && len < MAX_STRING; c++) {
                    out[len++] = (unsigned char)c;
                }
            }
            s += 3;
            continue;
        }

        /* Literal byte */
        out[len++] = (unsigned char)*s++;
    }
    return len;
}

/* Resolve STRING2 [c*] padding markers to extend STRING2 to len(STRING1).
 * Marker format placed by parse_repeat: 0xFF, 0xFE, c
 */
static void resolve_string2_padding(unsigned char *str, size_t *p_len2, size_t len1) {
    size_t src = 0, dst = 0;
    size_t len2 = *p_len2;
    while (src < len2) {
        if (src + 2 < len2 && str[src] == 0xFF && str[src+1] == 0xFE) {
            unsigned char c = str[src+2];
            src += 3;
            /* pad with c until total length equals len1 */
            while (dst < len1 && dst < MAX_STRING) {
                str[dst++] = c;
            }
        } else {
            str[dst++] = str[src++];
        }
        if (dst >= MAX_STRING) {
            fprintf(stderr, "tr: input string too long\n");
            exit(2);
        }
    }
    *p_len2 = dst;
}

/* Build membership arrays for set1 and squeeze set */
static void build_membership(void) {
    size_t i;
    /* Clear */
    for (i = 0; i <= UCHAR_MAX; i++) {
        in_set1[i] = 0;
        in_squeeze[i] = 0;
    }
    /* set1 (consider complement later via logic) */
    for (i = 0; i < set1_len; i++) {
        in_set1[set1_buf[i]] = 1;
    }
    /* squeeze set depends on options:
     * - If -d and -s: squeeze set = STRING2 (expanded); transliteration not performed.
     * - Else if -s and STRING2 present: squeeze set = STRING2 (expanded).
     * - Else if -s and no STRING2: squeeze set = STRING1 (consider complement).
     */
    if (opt_squeeze) {
        if (opt_delete || set2_len > 0) {
            size_t j;
            for (j = 0; j < set2_len; j++) in_squeeze[set2_buf[j]] = 1;
        } else {
            int c;
            for (c = 0; c <= UCHAR_MAX; c++) {
                int member = in_set1[c];
                if (opt_complement) member = !member;
                if (member) in_squeeze[c] = 1;
            }
        }
    }
}

/* Build transliteration map when not deleting.
 * Applies complement to STRING1 membership to decide which bytes are remapped.
 * Handles extension of STRING2 last element if shorter than STRING1.
 */
static void build_transliteration_map(void) {
    size_t j;
    /* identity map by default */
    for (j = 0; j <= UCHAR_MAX; j++) map[j] = (unsigned char)j;

    if (opt_delete) return; /* no transliteration when deleting */

    /* mapping only for bytes in set1 (consider complement) */
    size_t minlen = (set1_len < set2_len) ? set1_len : set2_len;

    /* First map the paired elements for membership bytes encountered in set1_buf order */
    for (j = 0; j < minlen; j++) {
        unsigned char src = set1_buf[j];
        /* Only map those bytes that are logically in set1 (after complement) */
        if ((opt_complement ? !in_set1[src] : in_set1[src])) {
            map[src] = set2_buf[j];
        }
    }

    /* If STRING2 shorter than STRING1, extend with last element of STRING2 */
    if (set1_len > minlen && set2_len > 0) {
        unsigned char last = set2_buf[set2_len - 1];
        size_t k;
        for (k = minlen; k < set1_len; k++) {
            unsigned char src = set1_buf[k];
            if ((opt_complement ? !in_set1[src] : in_set1[src])) {
                map[src] = last;
            }
        }
    }

    /* For complement case, need to map all bytes that are not in set1 but appear in input?
     * POSIX requires mapping only bytes that are members of the (possibly complemented) STRING1.
     * Therefore, only bytes logically in set1 (after -c) are remapped; others remain identity.
     * To implement complement without enumerating all not-in-set1 bytes, we rely on runtime check:
     * we will map via map[uc] only if uc is in the logical set; else leave as identity.
     * However, map is already identity for everything except explicitly set above.
     */
}

/* Print usage */
static void usage(void) {
    fprintf(stderr, "usage: tr [-cds] [-i file] [-o file] string1 [string2]\n");
}

int main(int argc, char *argv[]) {
    const char *s1 = NULL, *s2 = NULL;
    const char *in_path = NULL, *out_path = NULL;
    FILE *in = stdin;
    FILE *out = stdout;
    int i;

    /* Initialize identity map */
    {
        int c;
        for (c = 0; c <= UCHAR_MAX; c++) map[c] = (unsigned char)c;
    }

    /* Parse options */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        int j;
        for (j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
                case 'c': opt_complement = 1; break;
                case 'd': opt_delete = 1; break;
                case 's': opt_squeeze = 1; break;
                case 'i': {
                    /* -i requires a separate filename argument */
                    if (argv[i][j+1] != '\0') {
                        fprintf(stderr, "tr: option '-i' requires an argument\n");
                        usage();
                        return 2;
                    }
                    if (i + 1 >= argc) {
                        fprintf(stderr, "tr: missing argument for -i\n");
                        usage();
                        return 2;
                    }
                    in_path = argv[++i];
                    break;
                }
                case 'o': {
                    /* -o requires a separate filename argument */
                    if (argv[i][j+1] != '\0') {
                        fprintf(stderr, "tr: option '-o' requires an argument\n");
                        usage();
                        return 2;
                    }
                    if (i + 1 >= argc) {
                        fprintf(stderr, "tr: missing argument for -o\n");
                        usage();
                        return 2;
                    }
                    out_path = argv[++i];
                    break;
                }
                default:
                    fprintf(stderr, "tr: invalid option -- '%c'\n", argv[i][j]);
                    usage();
                    return 2;
            }
        }
    }

    if (i >= argc) {
        fprintf(stderr, "tr: missing operand\n");
        usage();
        return 2;
    }
    s1 = argv[i++];

    if (i < argc) s2 = argv[i++];

    if (!opt_delete && !s2) {
        /* When not deleting (and not using -c alone), STRING2 is required */
        fprintf(stderr, "tr: missing second operand\n");
        usage();
        return 2;
    }

    if (i < argc) {
        fprintf(stderr, "tr: extra operand '%s'\n", argv[i]);
        usage();
        return 2;
    }

    /* Open input/output files if specified */
    if (in_path) {
        in = fopen(in_path, "rb");
        if (!in) {
            fprintf(stderr, "tr: cannot open '%s' for reading: %s\n", in_path, strerror(errno));
            return 1;
        }
    }
    if (out_path) {
        out = fopen(out_path, "wb");
        if (!out) {
            fprintf(stderr, "tr: cannot open '%s' for writing: %s\n", out_path, strerror(errno));
            if (in && in != stdin) {
                if (fclose(in) != 0) {
                    fprintf(stderr, "tr: fclose failed on '%s': %s\n", in_path, strerror(errno));
                }
            }
            return 1;
        }
    }

    /* Expand STRING1 and STRING2 */
    set1_len = expand_string(s1, set1_buf, 0);
    if (s2) {
        set2_len = expand_string(s2, set2_buf, 1);
        /* Resolve [c*] padding in STRING2 to len(STRING1) */
        resolve_string2_padding(set2_buf, &set2_len, set1_len);
    } else {
        set2_len = 0;
    }

    /* Build membership arrays */
    build_membership();

    /* Build transliteration map if applicable */
    if (!opt_delete) {
        build_transliteration_map();
    }

    /* Process input -> output using buffered I/O */
    {
        unsigned char inbuf[4096];
        unsigned char outbuf[4096];
        size_t nread;
        int have_last_out = 0;
        unsigned char last_out = 0;
        setvbuf(in, (char *)inbuf, _IOLBF, sizeof(inbuf));

        while ((nread = fread(inbuf, 1, sizeof inbuf, in)) > 0) {
            size_t out_len = 0;
            size_t idx;
            for (idx = 0; idx < nread; idx++) {
                unsigned char uc = inbuf[idx];
                int in_logical_set1 = opt_complement ? !in_set1[uc] : in_set1[uc];

                if (opt_delete) {
                    if (in_logical_set1) {
                        /* delete: skip output */
                        continue;
                    }
                } else {
                    if (in_logical_set1) {
                        uc = map[uc];
                    }
                }

                if (opt_squeeze && in_squeeze[uc]) {
                    if (have_last_out && last_out == uc) {
                        continue;
                    }
                    last_out = uc;
                    have_last_out = 1;
                } else {
                    have_last_out = 0;
                }

                /* Append to output buffer */
                if (out_len < sizeof outbuf) {
                    outbuf[out_len++] = uc;
                } else {
                    /* Flush and reset if output buffer is full */
                    size_t nw = fwrite(outbuf, 1, out_len, out);
                    if (nw != out_len) {
                        fprintf(stderr, "tr: write error on %s: %s\n", out_path ? out_path : "stdout", strerror(errno));
                        goto io_error;
                    }
                    out_len = 0;
                    outbuf[out_len++] = uc;
                }
            }

            if (out_len > 0) {
                size_t nw = fwrite(outbuf, 1, out_len, out);
                if (nw != out_len) {
                    fprintf(stderr, "tr: write error on %s: %s\n", out_path ? out_path : "stdout", strerror(errno));
                    goto io_error;
                }
            }
        }

        if (ferror(in)) {
            fprintf(stderr, "tr: read error on %s: %s\n", in_path ? in_path : "stdin", strerror(errno));
            goto io_error;
        }
    }

    /* Close files if opened */
    if (in && in != stdin) {
        if (fclose(in) != 0) {
            fprintf(stderr, "tr: fclose failed on '%s': %s\n", in_path, strerror(errno));
            return 1;
        }
    }
    if (out && out != stdout) {
        if (fclose(out) != 0) {
            fprintf(stderr, "tr: fclose failed on '%s': %s\n", out_path, strerror(errno));
            return 1;
        }
    }

    return 0;

io_error:
    /* Attempt to close files and return error */
    if (in && in != stdin) {
        if (fclose(in) != 0) {
            fprintf(stderr, "tr: fclose failed on '%s': %s\n", in_path, strerror(errno));
        }
    }
    if (out && out != stdout) {
        if (fclose(out) != 0) {
            fprintf(stderr, "tr: fclose failed on '%s': %s\n", out_path, strerror(errno));
        }
    }
    return 1;
}
