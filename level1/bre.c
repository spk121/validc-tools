// Minimal BRE engine for POSIX ed: supports ., *, ^, $, bracket classes,
// basic escaped literals, and simple capture groups \( ... \) without nesting.
// Backreferences in pattern are not supported; replacement supports \1-\9.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "bre.h"

// NOTE on minimal semantics and deviations:
// - Inside bracket expressions [ ... ], we do not treat backslash as a
//   general escape. This matches common real-world tools (e.g., GNU),
//   allowing patterns like "[a\[]" to match a literal '['. POSIX says only
//   ']', '-', and sometimes '^' have special meaning inside classes, so this
//   is an acceptable simplification for this minimal engine.
// - Malformed patterns with unmatched "\(" or "\)" are treated as no-match
//   (find_group_end() fails and we return -1), rather than producing a
//   parse-time error. Upstream ed would reject such patterns at parse time;
//   for our internal use this behavior is sufficient.
typedef struct {
    const char *pat;
    int plen;
} Pat;

static int find_group_end(const char *pat, int pi, int pend) {
    // pat[pi] == '\\', pat[pi+1] == '('
    for (int i = pi + 2; i < pend - 1; i++) {
        if (pat[i] == '\\' && pat[i + 1] == ')') return i; // position of '\\'
    }
    return -1; // not found
}

static bool in_char_class(unsigned char c, const char *pat, int pi, int pend, int *after) {
    // pat[pi] == '['
    bool invert = false;
    int i = pi + 1;
    if (i < pend && pat[i] == '^') { invert = true; i++; }
    bool matched = false;
    unsigned char prev = 0;
    bool has_prev = false;
    for (; i < pend && pat[i] != ']'; i++) {
        if (pat[i] == '-' && has_prev && (i + 1) < pend && pat[i + 1] != ']') {
            unsigned char start = prev;
            unsigned char end = (unsigned char)pat[i + 1];
            if (start <= c && c <= end) matched = true;
            i++; // skip end
            has_prev = false;
            prev = 0;
        } else {
            if ((unsigned char)pat[i] == c) matched = true;
            prev = (unsigned char)pat[i];
            has_prev = true;
        }
    }
    if (i >= pend || pat[i] != ']') return false; // syntax error
    *after = i + 1;
    return invert ? !matched : matched;
}

// Forward declaration
static int match_here(const char *base, const char *text, int ti, const char *pat, int pi, int pend, BreMatch *m, int *group_id);

static int match_group_once(const char *base, const char *text, int ti, const char *pat, int gstart, int gend, BreMatch *m, int group_no) {
    int gid_tmp = group_no; // do not increment numbering inside
    int consumed = match_here(base, text, ti, pat, gstart, gend, m, &gid_tmp);
    if (consumed < 0) return -1;
    // Record capture for group_no (1-based numbering assigned by encounter order externally)
    int abs_start = ti;
    m->groups[group_no - 1].start = abs_start;
    m->groups[group_no - 1].length = consumed;
    if (group_no > m->num_groups) m->num_groups = group_no;
    return consumed;
}

static int match_here(const char *base, const char *text, int ti, const char *pat, int pi, int pend, BreMatch *m, int *group_id) {
    if (pi >= pend) return 0; // matched to end of pattern

    // End anchor at end of pattern
    if (pat[pi] == '$' && pi + 1 == pend) {
        return text[ti] == '\0' ? 0 : -1;
    }

    // Group \( ... \)
    if (pat[pi] == '\\' && pi + 1 < pend && pat[pi + 1] == '(') {
        int gend = find_group_end(pat, pi, pend);
        if (gend < 0) return -1;
        int group_no = *group_id + 1;
        *group_id = group_no;
        int consumed = match_group_once(base, text, ti, pat, pi + 2, gend, m, group_no);
        if (consumed < 0) return -1;
        int rest = match_here(base, text, ti + consumed, pat, gend + 2, pend, m, group_id);
        if (rest < 0) return -1;
        return consumed + rest;
    }

    // Character class
    if (pat[pi] == '[') {
        int after = 0;
        if (text[ti] == '\0') return -1;
        if (!in_char_class((unsigned char)text[ti], pat, pi, pend, &after)) return -1;
        if (after < pend && pat[after] == '*') {
            int tcur = ti;
            int after_local = 0;
            while (text[tcur] && in_char_class((unsigned char)text[tcur], pat, pi, pend, &after_local)) tcur++;
            int next_pi = after + 1;
            for (int ttry = tcur; ttry >= ti; ttry--) {
                int rest = match_here(base, text, ttry, pat, next_pi, pend, m, group_id);
                if (rest >= 0) return (ttry - ti) + rest;
            }
            return -1;
        } else {
            int rest = match_here(base, text, ti + 1, pat, after, pend, m, group_id);
            if (rest < 0) return -1;
            return 1 + rest;
        }
    }

    // Dot '.'
    if (pat[pi] == '.') {
        if (text[ti] == '\0') return -1;
        if (pi + 1 < pend && pat[pi + 1] == '*') {
            int tcur = ti;
            while (text[tcur]) tcur++;
            int next_pi = pi + 2;
            for (int ttry = tcur; ttry >= ti; ttry--) {
                int rest = match_here(base, text, ttry, pat, next_pi, pend, m, group_id);
                if (rest >= 0) return (ttry - ti) + rest;
            }
            return -1;
        } else {
            int rest = match_here(base, text, ti + 1, pat, pi + 1, pend, m, group_id);
            if (rest < 0) return -1;
            return 1 + rest;
        }
    }

    // Escaped literal (treat \) as error here)
    if (pat[pi] == '\\' && pi + 1 < pend) {
        char esc = pat[pi + 1];
        if (esc == ')') return -1;
        if (text[ti] == '\0' || text[ti] != esc) return -1;
        if (pi + 2 < pend && pat[pi + 2] == '*') {
            int tcur = ti;
            while (text[tcur] == esc) tcur++;
            int next_pi = pi + 3;
            for (int ttry = tcur; ttry >= ti; ttry--) {
                int rest = match_here(base, text, ttry, pat, next_pi, pend, m, group_id);
                if (rest >= 0) return (ttry - ti) + rest;
            }
            return -1;
        } else {
            int rest = match_here(base, text, ti + 1, pat, pi + 2, pend, m, group_id);
            if (rest < 0) return -1;
            return 1 + rest;
        }
    }

    // Literal
    if (text[ti] == '\0' || text[ti] != pat[pi]) return -1;
    if (pi + 1 < pend && pat[pi + 1] == '*') {
        int tcur = ti;
        while (text[tcur] == pat[pi]) tcur++;
        int next_pi = pi + 2;
        for (int ttry = tcur; ttry >= ti; ttry--) {
            int rest = match_here(base, text, ttry, pat, next_pi, pend, m, group_id);
            if (rest >= 0) return (ttry - ti) + rest;
        }
        return -1;
    } else {
        int rest = match_here(base, text, ti + 1, pat, pi + 1, pend, m, group_id);
        if (rest < 0) return -1;
        return 1 + rest;
    }
}

bool bre_match(const char *text, const char *pattern, BreMatch *match) {
    if (!text || !pattern || !match) return false;

    match->start = -1;
    match->length = 0;
    match->num_groups = 0;
    for (int i = 0; i < BRE_MAX_GROUPS; i++) {
        match->groups[i].start = -1;
        match->groups[i].length = 0;
    }

    int plen = (int)strlen(pattern);
    bool anchored_start = (plen > 0 && pattern[0] == '^');
    int start_pi = anchored_start ? 1 : 0;

    if (anchored_start) {
        int gid = 0;
        int consumed = match_here(text, text, 0, pattern, start_pi, plen, match, &gid);
        if (consumed >= 0) {
            match->start = 0;
            match->length = consumed;
            return true;
        }
        return false;
    }

    // Try each position
    int tlen = (int)strlen(text);
    for (int s = 0; s <= tlen; s++) {
        // reset groups each try
        match->num_groups = 0;
        for (int i = 0; i < BRE_MAX_GROUPS; i++) { match->groups[i].start = -1; match->groups[i].length = 0; }
        int gid = 0;
        int consumed = match_here(text, text, s, pattern, start_pi, plen, match, &gid);
        if (consumed >= 0) {
            match->start = s;
            match->length = consumed;
            return true;
        }
    }
    return false;
}

char *bre_substitute(const char *text, const char *pattern, const char *replacement) {
    if (!text || !pattern || !replacement) return NULL;

    BreMatch m;
    if (!bre_match(text, pattern, &m) || m.start < 0) {
        // No match: return copy
        size_t n = strlen(text) + 1;
        char *out = (char*)malloc(n);
        if (!out) return NULL;
        memcpy(out, text, n);
        return out;
    }

    size_t prefix_len = (size_t)m.start;
    size_t suffix_len = strlen(text) - (size_t)m.start - (size_t)m.length;

    // Compute replacement length
    size_t rep_len = 0;
    for (const char *r = replacement; *r; r++) {
        if (*r == '\\' && r[1] >= '1' && r[1] <= '9') {
            int g = r[1] - '1';
            if (g >= 0 && g < m.num_groups && m.groups[g].start >= 0) rep_len += (size_t)m.groups[g].length;
            r++;
        } else {
            rep_len++;
        }
    }

    size_t out_len = prefix_len + rep_len + suffix_len;
    char *out = (char*)malloc(out_len + 1);
    if (!out) return NULL;

    // Copy prefix
    memcpy(out, text, prefix_len);
    char *p = out + prefix_len;

    // Build replacement
    for (const char *r = replacement; *r; r++) {
        if (*r == '\\' && r[1] >= '1' && r[1] <= '9') {
            int g = r[1] - '1';
            if (g >= 0 && g < m.num_groups && m.groups[g].start >= 0) {
                memcpy(p, text + m.groups[g].start, (size_t)m.groups[g].length);
                p += m.groups[g].length;
            }
            r++;
        } else {
            *p++ = *r;
        }
    }

    // Copy suffix
    memcpy(p, text + m.start + m.length, suffix_len);
    out[out_len] = '\0';
    return out;
}
