// Minimal BRE engine for POSIX ed: supports ., *, ^, $, bracket classes,
// basic escaped literals, and simple capture groups \( ... \) without nesting.
// Backreferences in pattern are not supported; replacement supports \1-\9.

#include "bre.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RE_DUP_MAX 255

typedef struct
{
    const char *pat;
    int plen;
} Pat;

/* Atom once function signature */
typedef int (*AtomOnceFn)(MatchContext *ctx, int atom_start, int atom_end, int tpos, BreMatch *m, int *group_id);

// Forward declaration
static int match_here(MatchContext *ctx, BreMatch *m, int *group_id);
static bool in_char_class(unsigned char c, const char *pat, int pi, int pend, int *after);
static int find_group_end(const char *pat, int pi, int pend);
int parse_bre_repetition(const char *pat, int pi, int pend, int *min_rep, int *max_rep, int *next_pi);

static int find_group_end(const char *pat, int pi, int pend)
{
    // pat[pi] == '\\', pat[pi+1] == '('
    for (int i = pi + 2; i < pend - 1; i++)
    {
        if (pat[i] == '\\' && pat[i + 1] == ')')
            return i; // position of '\\'
    }
    return -1; // not found
}

static bool in_char_class(unsigned char c, const char *pat, int pi, int pend, int *after)
{
    // pat[pi] == '['
    bool invert = false;
    int i = pi + 1;
    if (i < pend && pat[i] == '^')
    {
        invert = true;
        i++;
    }
    bool matched = false;
    unsigned char prev = 0;
    bool has_prev = false;
    for (; i < pend && pat[i] != ']'; i++)
    {
        if (pat[i] == '-' && has_prev && (i + 1) < pend && pat[i + 1] != ']')
        {
            unsigned char start = prev;
            unsigned char end = (unsigned char)pat[i + 1];
            if (start <= c && c <= end)
                matched = true;
            i++; // skip end
            has_prev = false;
            prev = 0;
        }
        else
        {
            if ((unsigned char)pat[i] == c)
                matched = true;
            prev = (unsigned char)pat[i];
            has_prev = true;
        }
    }
    if (i >= pend || pat[i] != ']')
        return false; // syntax error
    *after = i + 1;
    return invert ? !matched : matched;
}

/* Unified quantifier parser: *, \+, \{n,m\} */
static int parse_quantifier(const char *pat, int at, int pend, int *min, int *max, int *next_pi)
{
    if (at < pend && pat[at] == '*')
    {
        *min = 0;
        *max = -1;
        *next_pi = at + 1;
        return 1;
    }
    if (at + 1 < pend && pat[at] == '\\' && pat[at + 1] == '+')
    {
        *min = 1;
        *max = -1;
        *next_pi = at + 2;
        return 1;
    }
    return parse_bre_repetition(pat, at, pend, min, max, next_pi);
}

/* --------------------------------------------------------------
   BRE repetition parser:  \{n\}   \{n,\}   \{n,m\}
   Returns:
       0 = no repetition (not starting with \{)
       1 = valid repetition found
      -1 = malformed (unterminated, bad syntax, etc.)
   If valid, fills *min_rep, *max_rep, and *next_pi
   -------------------------------------------------------------- */
int parse_bre_repetition(const char *pat, int pi, int pend, int *min_rep, int *max_rep, int *next_pi)
{
    if (pi + 5 > pend)
    {
        // too short for \{n\}
        if (pat[pi] == '\\' && pi + 1 < pend && pat[pi + 1] == '{')
            // Open brace without close brace.
            return -1;
        // No open brace.
        return 0;
    }
    if (pat[pi] != '\\' || pat[pi + 1] != '{')
        return 0;

    int j = pi + 2; // first char after \{
    const char *start = pat + j;

    // --- parse first number (mandatory) ---
    if (j >= pend || !isdigit((unsigned char)pat[j]))
        return -1;
    while (j < pend && isdigit((unsigned char)pat[j]))
        j++;

    *min_rep = atoi(start);

    if (*min_rep > RE_DUP_MAX)
        return -1;

    // --- comma or closing brace ---
    if (j >= pend)
        return -1;

    if (pat[j] == '\\' && j + 1 < pend && pat[j + 1] == '}')
    {
        // \{n\}
        *max_rep = *min_rep;
        *next_pi = j + 2;
        return 1;
    }

    if (pat[j] != ',')
        return -1; // garbage
    j++;           // skip comma

    if (j >= pend)
        return -1;

    if (pat[j] == '\\' && j + 1 < pend && pat[j + 1] == '}')
    {
        // \{n,\} -> n or more
        *max_rep = -1; // unbounded
        *next_pi = j + 2;
        return 1;
    }

    // --- parse second number ---
    start = pat + j;
    if (!isdigit((unsigned char)pat[j]))
        return -1;
    while (j < pend && isdigit((unsigned char)pat[j]))
        j++;

    *max_rep = atoi(start);

    if (*max_rep > RE_DUP_MAX || *min_rep > *max_rep)
        return -1;

    if (j + 1 >= pend || pat[j] != '\\' || pat[j + 1] != '}')
        return -1;

    *next_pi = j + 2;
    return 1;
}

/* 3) Exact backtracking repetition application */
static int match_repeated(MatchContext *ctx, AtomOnceFn atom_once, int atom_start, int atom_end, int min_rep,
                          int max_rep, int next_pi, BreMatch *m, int *group_id)
{
    int tpos = ctx->ti;
    int max_possible = (max_rep < 0) ? 10000 : max_rep;

    int advs[256];
    int have = 0;

    /* Greedy consume occurrences */
    while (have < max_possible)
    {
        int adv = atom_once(ctx, atom_start, atom_end, tpos, m, group_id);
        if (adv < 0)
            break;
        if (have < (int)(sizeof(advs) / sizeof(advs[0])))
        {
            advs[have] = adv;
        }
        else
        {
            break; /* cap reached */
        }
        tpos += adv;
        have++;
    }

    /* Backtrack to satisfy min and match rest */
    while (have >= min_rep)
    {
        MatchContext rest_ctx = *ctx;
        rest_ctx.ti = tpos;
        rest_ctx.pi = next_pi;

        int gid_save = *group_id;
        int rest = match_here(&rest_ctx, m, &gid_save);
        if (rest >= 0)
        {
            *group_id = gid_save;
            return (tpos - ctx->ti) + rest;
        }

        if (have == 0)
            break;
        /* Exact backoff by last occurrence length */
        {
            int last_adv = (have <= (int)(sizeof(advs) / sizeof(advs[0]))) ? advs[have - 1] : 1;
            tpos -= last_adv;
        }
        have--;
    }
    return -1;
}

/* 4) Tiny atom-once implementations (no repetition, no rest, minimal side effects) */

/* Literal: one char not special */
static int once_literal(MatchContext *ctx, int atom_start, int atom_end, int tpos, BreMatch *m, int *group_id)
{
    (void)m;
    (void)group_id;
    (void)atom_end;
    if (tpos < 0 || ctx->text[tpos] == '\0')
        return -1;

    char ch = ctx->pat[atom_start];
    if (ch == '\\' || ch == '[' || ch == '.' || ch == '^' || ch == '$' || ch == '*')
        return -1;

    return (ctx->text[tpos] == ch) ? 1 : -1;
}

/* Dot: any single character except NUL */
static int once_dot(MatchContext *ctx, int atom_start, int atom_end, int tpos, BreMatch *m, int *group_id)
{
    (void)ctx;
    (void)atom_start;
    (void)atom_end;
    (void)m;
    (void)group_id;
    return (ctx->text[tpos] != '\0') ? 1 : -1;
}

/* Escape: \X where X is not (,),{,} */
static int once_escape(MatchContext *ctx, int atom_start, int atom_end, int tpos, BreMatch *m, int *group_id)
{
    (void)m;
    (void)group_id;
    if (atom_start + 1 >= ctx->pend || ctx->text[tpos] == '\0')
        return -1;
    char esc = ctx->pat[atom_start + 1];
    if (esc == '(' || esc == ')' || esc == '{' || esc == '}')
        return -1;
    return (ctx->text[tpos] == esc) ? 1 : -1;
}

/* Class: [ ... ] using existing in_char_class */
static int once_class(MatchContext *ctx, int atom_start, int atom_end, int tpos, BreMatch *m, int *group_id)
{
    (void)m;
    (void)group_id;
    (void)atom_end;
    if (ctx->text[tpos] == '\0')
        return -1;
    {
        int after_dummy = 0;
        return in_char_class((unsigned char)ctx->text[tpos], ctx->pat, atom_start, ctx->pend, &after_dummy) ? 1 : -1;
    }
}

/* Group inner: match inner content once from tpos; no capture recording here */
static int once_group_inner(MatchContext *ctx, int inner_start, int inner_end, int tpos, BreMatch *m, int *group_id)
{
    MatchContext inner = *ctx;
    inner.ti = tpos;
    inner.pi = inner_start;
    inner.pend = inner_end;
    /* Note: We keep *group_id stable here (no nesting), match_here will manage numbering */
    int gid_tmp = *group_id;
    int adv = match_here(&inner, m, &gid_tmp);
    if (adv < 0)
        return -1;
    return adv;
}

/* 5) Utilities to locate atom bounds */

/* Simple class end: returns index just after ']' or -1 */
static int find_class_end_simple(const char *pat, int pi, int pend)
{
    int i = pi + 1;
    if (i < pend && pat[i] == '^')
        i++;
    for (; i < pend; ++i)
    {
        if (pat[i] == ']')
            return i + 1;
    }
    return -1;
}

/* 6) Group matching using unified quantifiers with capture recording on success */
static int match_group(MatchContext *ctx, BreMatch *m, int *group_id)
{
    if (!(ctx->pat[ctx->pi] == '\\' && ctx->pi + 1 < ctx->pend && ctx->pat[ctx->pi + 1] == '('))
    {
        return 0; /* not a group at this position */
    }

    int gend = find_group_end(ctx->pat, ctx->pi, ctx->pend);
    if (gend < 0)
        return -1; /* malformed */

    int gid_orig = *group_id;
    int group_no = gid_orig + 1;
    int gidx = group_no - 1;

    int atom_start = ctx->pi;
    int atom_end = gend + 2;
    int inner_start = ctx->pi + 2;
    int inner_end = gend;

    /* Parse optional quantifier after the group */
    int min_rep = 1, max_rep = 1, next_pi = atom_end;
    int qres = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
    if (qres < 0)
        return -1;

    /* No quantifier: match inner once, then rest; record capture only after success */
    if (qres == 0)
    {
        /* Special-case inner == ".*" so we can backtrack across the group boundary */
        if (inner_end - inner_start == 2 && ctx->pat[inner_start] == '.' && ctx->pat[inner_start + 1] == '*')
        {

            int next_after_group = atom_end;
            /* Greedy consume entire remaining text */
            int tcur = ctx->ti;
            while (ctx->text[tcur])
                tcur++;

            for (int ttry = tcur; ttry >= ctx->ti; --ttry)
            {
                MatchContext rest_ctx = *ctx;
                rest_ctx.ti = ttry;
                rest_ctx.pi = next_after_group;

                int gid_tmp = gid_orig + 1; /* consume group number if rest matches */
                int rest = match_here(&rest_ctx, m, &gid_tmp);
                if (rest >= 0)
                {
                    m->groups[gidx].start = ctx->ti;
                    m->groups[gidx].length = ttry - ctx->ti;
                    if (group_no > m->num_groups)
                        m->num_groups = group_no;
                    *group_id = gid_tmp;
                    return (ttry - ctx->ti) + rest;
                }
            }
            return -1; /* no position worked */
        }

        int adv = once_group_inner(ctx, inner_start, inner_end, ctx->ti, m, group_id);
        if (adv < 0)
            return -1;

        MatchContext rest_ctx = *ctx;
        rest_ctx.ti = ctx->ti + adv;
        rest_ctx.pi = atom_end;

        int gid_tmp = gid_orig + 1; /* consume group number */
        int rest = match_here(&rest_ctx, m, &gid_tmp);
        if (rest < 0)
            return -1;

        m->groups[gidx].start = ctx->ti;
        m->groups[gidx].length = adv;
        if (group_no > m->num_groups)
            m->num_groups = group_no;

        *group_id = gid_tmp;
        return adv + rest;
    }

    /* With quantifier: use match_repeated with a small wrapper to call inner once */
    /* Wrapper function adapting inner_start/end to AtomOnceFn signature */
    int group_once(MatchContext * c, int astart, int aend, int tpos, BreMatch *mm, int *gid)
    {
        (void)astart;
        (void)aend;
        /* Keep outer group numbering stable during speculative inner matches */
        int gid_save = *gid;
        int adv = once_group_inner(c, inner_start, inner_end, tpos, mm, &gid_save);
        return adv;
    }

    /* Greedy repetition + exact backtracking */
    int tpos = ctx->ti;
    int max_possible = (max_rep < 0) ? 10000 : max_rep;

    int advs[256];
    int have = 0;

    while (have < max_possible)
    {
        int adv = group_once(ctx, atom_start, atom_end, tpos, m, group_id);
        if (adv < 0)
            break;
        if (have < (int)(sizeof(advs) / sizeof(advs[0])))
        {
            advs[have] = adv;
        }
        else
        {
            break;
        }
        tpos += adv;
        have++;
    }

    while (have >= min_rep)
    {
        MatchContext rest_ctx = *ctx;
        rest_ctx.ti = tpos;
        rest_ctx.pi = next_pi;

        int gid_tmp_rest = gid_orig + 1; /* consume this group number now */
        int rest = match_here(&rest_ctx, m, &gid_tmp_rest);
        if (rest >= 0)
        {
            /* Record the full span of group repetition */
            m->groups[gidx].start = ctx->ti;
            m->groups[gidx].length = tpos - ctx->ti;
            if (group_no > m->num_groups)
                m->num_groups = group_no;

            *group_id = gid_tmp_rest;
            return (tpos - ctx->ti) + rest;
        }

        if (have == 0)
            break;
        {
            int last_adv = (have <= (int)(sizeof(advs) / sizeof(advs[0]))) ? advs[have - 1] : 1;
            tpos -= last_adv;
        }
        have--;
    }

    return -1;
}

/* 7) Updated match_here: dispatch to atom once + unified quantifiers */

static int match_here(MatchContext *ctx, BreMatch *m, int *group_id)
{
    if (ctx->pi >= ctx->pend)
        return 0; /* end of pattern */

    /* End anchor: $ at end */
    if (ctx->pat[ctx->pi] == '$' && ctx->pi + 1 == ctx->pend)
    {
        return ctx->text[ctx->ti] == '\0' ? 0 : -1;
    }

    /* Group first */
    {
        int gret = match_group(ctx, m, group_id);
        if (gret != 0)
            return gret;
    }

    /* Class */
    if (ctx->pat[ctx->pi] == '[')
    {
        int atom_start = ctx->pi;
        int atom_end = find_class_end_simple(ctx->pat, atom_start, ctx->pend);
        if (atom_end < 0)
            return -1;

        int min_rep = 1, max_rep = 1, next_pi = atom_end;
        int qres = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
        if (qres < 0)
            return -1;

        return match_repeated(ctx, once_class, atom_start, atom_end, min_rep, max_rep, next_pi, m, group_id);
    }

    /* Dot */
    if (ctx->pat[ctx->pi] == '.')
    {
        int atom_start = ctx->pi;
        int atom_end = ctx->pi + 1;

        int min_rep = 1, max_rep = 1, next_pi = atom_end;
        int qres = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
        if (qres < 0)
            return -1;

        return match_repeated(ctx, once_dot, atom_start, atom_end, min_rep, max_rep, next_pi, m, group_id);
    }

    /* Escape (non-group) */
    if (ctx->pat[ctx->pi] == '\\')
    {
        if (ctx->pi + 1 < ctx->pend)
        {
            char esc = ctx->pat[ctx->pi + 1];
            if (esc == '(')
            {
                /* Already handled by match_group above */
                return -1; /* syntax error if we get here */
            }
            if (esc == ')' || esc == '{' || esc == '}')
            {
                return -1; /* invalid here */
            }

            int atom_start = ctx->pi;
            int atom_end = ctx->pi + 2;

            int min_rep = 1, max_rep = 1, next_pi = atom_end;
            int qres = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
            if (qres < 0)
                return -1;

            return match_repeated(ctx, once_escape, atom_start, atom_end, min_rep, max_rep, next_pi, m, group_id);
        }
        return -1;
    }

    /* Literal */
    {
        int atom_start = ctx->pi;
        int atom_end = ctx->pi + 1;

        int min_rep = 1, max_rep = 1, next_pi = atom_end;
        int qres = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
        if (qres < 0)
            return -1;

        return match_repeated(ctx, once_literal, atom_start, atom_end, min_rep, max_rep, next_pi, m, group_id);
    }
}

bool bre_match(const char *text, const char *pattern, BreMatch *match)
{
    if (!text || !pattern || !match)
        return false;

    match->start = -1;
    match->length = 0;
    match->num_groups = 0;
    for (int i = 0; i < BRE_MAX_GROUPS; i++)
    {
        match->groups[i].start = -1;
        match->groups[i].length = 0;
    }

    int plen = (int)strlen(pattern);
    bool anchored_start = (plen > 0 && pattern[0] == '^');
    int start_pi = anchored_start ? 1 : 0;

    MatchContext ctx = {.base = text, .text = text, .ti = 0, .pat = pattern, .pi = start_pi, .pend = plen};

    if (anchored_start)
    {
        int gid = 0;
        int consumed = match_here(&ctx, match, &gid);
        if (consumed >= 0)
        {
            match->start = 0;
            match->length = consumed;
            return true;
        }
        return false;
    }

    // Try each position
    int tlen = (int)strlen(text);
    for (int s = 0; s <= tlen; s++)
    {
        // reset groups each try
        match->num_groups = 0;
        for (int i = 0; i < BRE_MAX_GROUPS; i++)
        {
            match->groups[i].start = -1;
            match->groups[i].length = 0;
        }
        int gid = 0;
        ctx.ti = s;
        int consumed = match_here(&ctx, match, &gid);
        if (consumed >= 0)
        {
            match->start = s;
            match->length = consumed;
            return true;
        }
    }
    return false;
}

char *bre_substitute(const char *text, const char *pattern, const char *replacement)
{
    if (!text || !pattern || !replacement)
        return NULL;

    BreMatch m;
    if (!bre_match(text, pattern, &m) || m.start < 0)
    {
        // No match: return copy
        size_t n = strlen(text) + 1;
        char *out = (char *)malloc(n);
        if (!out)
            return NULL;
        memcpy(out, text, n);
        return out;
    }

    size_t prefix_len = (size_t)m.start;
    size_t suffix_len = strlen(text) - (size_t)m.start - (size_t)m.length;

    // Compute replacement length
    size_t rep_len = 0;
    for (const char *r = replacement; *r; r++)
    {
        if (*r == '\\' && r[1] >= '1' && r[1] <= '9')
        {
            int g = r[1] - '1';
            if (g >= 0 && g < m.num_groups && m.groups[g].start >= 0)
                rep_len += (size_t)m.groups[g].length;
            r++;
        }
        else
        {
            rep_len++;
        }
    }

    size_t out_len = prefix_len + rep_len + suffix_len;
    char *out = (char *)malloc(out_len + 1);
    if (!out)
        return NULL;

    // Copy prefix
    memcpy(out, text, prefix_len);
    char *p = out + prefix_len;

    // Build replacement
    for (const char *r = replacement; *r; r++)
    {
        if (*r == '\\' && r[1] >= '1' && r[1] <= '9')
        {
            int g = r[1] - '1';
            if (g >= 0 && g < m.num_groups && m.groups[g].start >= 0)
            {
                memcpy(p, text + m.groups[g].start, (size_t)m.groups[g].length);
                p += m.groups[g].length;
            }
            r++;
        }
        else
        {
            *p++ = *r;
        }
    }

    // Copy suffix
    memcpy(p, text + m.start + m.length, suffix_len);
    out[out_len] = '\0';
    return out;
}
