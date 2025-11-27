// Patch: Fix group capture length regression by distinguishing group span from total match length.
// Adds prefix_out parameter to match_repeated so groups record only the repeated group's span, not including the
// remainder.

#include "bre.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RE_DUP_MAX 255

static BreResult match_here(MatchContext *ctx, BreMatch *m, int *group_id, int *total_out);
static bool in_char_class(unsigned char c, const char *pat, int pi, int pend, int *after);
static int find_group_end(const char *pat, int pi, int pend);

typedef BreResult (*AtomOnceFn)(MatchContext *ctx, int atom_start, int atom_end, int tpos, int *adv_out);

/* ---------- Helpers ---------- */

static int find_group_end(const char *pat, int pi, int pend)
{
    for (int i = pi + 2; i < pend - 1; i++)
        if (pat[i] == '\\' && pat[i + 1] == ')')
            return i;
    return -1;
}

static bool in_char_class(unsigned char c, const char *pat, int pi, int pend, int *after)
{
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
            i++;
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
        return false;
    *after = i + 1;
    return invert ? !matched : matched;
}

/* ---------- Quantifiers ---------- */

BreResult parse_bre_repetition(const char *pat, int pi, int pend, int *min_rep, int *max_rep, int *next_pi)
{
    if (pi + 5 > pend)
    {
        if (pi < pend && pat[pi] == '\\' && pi + 1 < pend && pat[pi + 1] == '{')
            return BRE_ERROR;
        return BRE_NOMATCH;
    }
    if (pat[pi] != '\\' || pat[pi + 1] != '{')
        return BRE_NOMATCH;

    int j = pi + 2;
    const char *start = pat + j;

    if (j >= pend || !isdigit((unsigned char)pat[j]))
        return BRE_ERROR;
    while (j < pend && isdigit((unsigned char)pat[j]))
        j++;
    *min_rep = atoi(start);
    if (*min_rep > RE_DUP_MAX)
        return BRE_ERROR;

    if (j >= pend)
        return BRE_ERROR;

    if (pat[j] == '\\' && j + 1 < pend && pat[j + 1] == '}')
    {
        *max_rep = *min_rep;
        *next_pi = j + 2;
        return BRE_OK;
    }

    if (pat[j] != ',')
        return BRE_ERROR;
    j++;
    if (j >= pend)
        return BRE_ERROR;

    if (pat[j] == '\\' && j + 1 < pend && pat[j + 1] == '}')
    {
        *max_rep = -1;
        *next_pi = j + 2;
        return BRE_OK;
    }

    start = pat + j;
    if (!isdigit((unsigned char)pat[j]))
        return BRE_ERROR;
    while (j < pend && isdigit((unsigned char)pat[j]))
        j++;
    *max_rep = atoi(start);
    if (*max_rep > RE_DUP_MAX || *min_rep > *max_rep)
        return BRE_ERROR;
    if (j + 1 >= pend || pat[j] != '\\' || pat[j + 1] != '}')
        return BRE_ERROR;

    *next_pi = j + 2;
    return BRE_OK;
}

static BreResult parse_quantifier(const char *pat, int at, int pend, int *min, int *max, int *next_pi)
{
    if (at < pend && pat[at] == '*')
    {
        *min = 0;
        *max = -1;
        *next_pi = at + 1;
        return BRE_OK;
    }
    if (at + 1 < pend && pat[at] == '\\' && pat[at + 1] == '+')
    {
        *min = 1;
        *max = -1;
        *next_pi = at + 2;
        return BRE_OK;
    }
    return parse_bre_repetition(pat, at, pend, min, max, next_pi);
}

/* ---------- Atom Once Implementations ---------- */

static BreResult once_literal(MatchContext *ctx, int atom_start, int atom_end, int tpos, int *adv_out)
{
    (void)atom_end;
    if (tpos < 0 || ctx->text[tpos] == '\0')
        return BRE_NOMATCH;
    char ch = ctx->pat[atom_start];
    if (ch == '\\' || ch == '[' || ch == '.' || ch == '^' || ch == '$' || ch == '*')
        return BRE_NOMATCH;
    if (ctx->text[tpos] != ch)
        return BRE_NOMATCH;
    *adv_out = 1;
    return BRE_OK;
}

static BreResult once_dot(MatchContext *ctx, int atom_start, int atom_end, int tpos, int *adv_out)
{
    (void)atom_start;
    (void)atom_end;
    if (ctx->text[tpos] == '\0')
        return BRE_NOMATCH;
    *adv_out = 1;
    return BRE_OK;
}

static BreResult once_escape(MatchContext *ctx, int atom_start, int atom_end, int tpos, int *adv_out)
{
    if (atom_start + 1 >= ctx->pend || ctx->text[tpos] == '\0')
        return BRE_NOMATCH;
    char esc = ctx->pat[atom_start + 1];
    if (esc == '(' || esc == ')' || esc == '{' || esc == '}')
        return BRE_NOMATCH;
    if (ctx->text[tpos] != esc)
        return BRE_NOMATCH;
    *adv_out = 1;
    return BRE_OK;
}

static BreResult once_class(MatchContext *ctx, int atom_start, int atom_end, int tpos, int *adv_out)
{
    (void)atom_end;
    if (ctx->text[tpos] == '\0')
        return BRE_NOMATCH;
    int after_dummy = 0;
    if (!in_char_class((unsigned char)ctx->text[tpos], ctx->pat, atom_start, ctx->pend, &after_dummy))
        return BRE_NOMATCH;
    *adv_out = 1;
    return BRE_OK;
}

/* ---------- Repetition with exact backtracking ----------
   Added prefix_out to report the span consumed by the repeated atom(s) (before remainder).
*/
static BreResult match_repeated(MatchContext *ctx, AtomOnceFn atom_once, int atom_start, int atom_end, int min_rep,
                                int max_rep, int next_pi, BreMatch *m, int *group_id, int *total_out, int *prefix_out)
{
    (void)m;
    int tpos = ctx->ti;
    int max_possible = (max_rep < 0) ? 10000 : max_rep;
    int advs[256];
    int have = 0;

    while (have < max_possible)
    {
        int adv = 0;
        BreResult r = atom_once(ctx, atom_start, atom_end, tpos, &adv);
        if (r == BRE_ERROR)
            return BRE_ERROR;
        if (r != BRE_OK)
            break;
        if (have < (int)(sizeof(advs) / sizeof(advs[0])))
            advs[have] = adv;
        tpos += adv;
        have++;
    }

    while (have >= min_rep)
    {
        MatchContext rest_ctx = *ctx;
        rest_ctx.ti = tpos;
        rest_ctx.pi = next_pi;

        int rest_total = 0;
        BreResult rr = match_here(&rest_ctx, m, group_id, &rest_total);
        if (rr == BRE_ERROR)
            return BRE_ERROR;
        if (rr == BRE_OK)
        {
            if (prefix_out)
                *prefix_out = tpos - ctx->ti;
            *total_out = (tpos - ctx->ti) + rest_total;
            return BRE_OK;
        }
        if (have == 0)
            break;
        int last_adv = (have <= (int)(sizeof(advs) / sizeof(advs[0]))) ? advs[have - 1] : 1;
        tpos -= last_adv;
        have--;
    }
    return BRE_NOMATCH;
}

/* ---------- Group Handling ---------- */

static BreResult once_group_inner(MatchContext *ctx, int inner_start, int inner_end, int tpos, BreMatch *m,
                                  int *group_id, int *adv_out)
{
    MatchContext inner = *ctx;
    inner.ti = tpos;
    inner.pi = inner_start;
    inner.pend = inner_end;
    int gid_tmp = group_id ? *group_id : 0;
    int consumed = 0;
    BreResult r = match_here(&inner, m, &gid_tmp, &consumed);
    if (r != BRE_OK)
        return r;
    *adv_out = consumed;
    return BRE_OK;
}

static BreResult match_group(MatchContext *ctx, BreMatch *m, int *group_id, int *total_out, bool *applied_out)
{
    *applied_out = false;
    if (!(ctx->pat[ctx->pi] == '\\' && ctx->pi + 1 < ctx->pend && ctx->pat[ctx->pi + 1] == '('))
        return BRE_NOMATCH;

    int gend = find_group_end(ctx->pat, ctx->pi, ctx->pend);
    if (gend < 0)
        return BRE_ERROR;

    int gid_orig = group_id ? *group_id : 0;
    int group_no = gid_orig + 1;
    int gidx = group_no - 1;

    int atom_end = gend + 2;
    int inner_start = ctx->pi + 2;
    int inner_end = gend;

    int min_rep = 1, max_rep = 1, next_pi = atom_end;
    BreResult qres = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
    if (qres == BRE_ERROR)
        return BRE_ERROR;

    /* Special-case \(.*\) without quantifier for outer backtracking */
    if (qres == BRE_NOMATCH && inner_end - inner_start == 2 && ctx->pat[inner_start] == '.' &&
        ctx->pat[inner_start + 1] == '*')
    {
        int tcur = ctx->ti;
        while (ctx->text[tcur])
            tcur++;
        for (int ttry = tcur; ttry >= ctx->ti; --ttry)
        {
            MatchContext rest_ctx = *ctx;
            rest_ctx.ti = ttry;
            rest_ctx.pi = atom_end;
            int rest_total = 0;
            BreResult rr = match_here(&rest_ctx, m, group_id, &rest_total);
            if (rr == BRE_ERROR)
                return BRE_ERROR;
            if (rr == BRE_OK)
            {
                m->groups[gidx].start = ctx->ti;
                m->groups[gidx].length = ttry - ctx->ti;
                if (group_no > m->num_groups)
                    m->num_groups = group_no;
                if (group_id)
                    *group_id = gid_orig + 1;
                *total_out = (ttry - ctx->ti) + rest_total;
                *applied_out = true;
                return BRE_OK;
            }
        }
        return BRE_NOMATCH;
    }

    /* General path: repetition on inner or single occurrence */
    BreResult inner_once_adapter(MatchContext * c, int astart, int aend, int tpos, int *adv_out)
    {
        (void)astart;
        (void)aend;
        return once_group_inner(c, inner_start, inner_end, tpos, m, group_id, adv_out);
    }

    int total = 0, group_span = 0;
    BreResult mr = match_repeated(ctx, inner_once_adapter, ctx->pi, atom_end, min_rep, max_rep, next_pi, m, group_id,
                                  &total, &group_span);
    if (mr != BRE_OK)
        return mr;

    m->groups[gidx].start = ctx->ti;
    m->groups[gidx].length = group_span;
    if (group_no > m->num_groups)
        m->num_groups = group_no;
    if (group_id)
        *group_id = gid_orig + 1;
    *total_out = total;
    *applied_out = true;
    return BRE_OK;
}

/* ---------- Core Dispatcher ---------- */

static int find_class_end_simple(const char *pat, int pi, int pend)
{
    int i = pi + 1;
    if (i < pend && pat[i] == '^')
        i++;
    for (; i < pend; ++i)
        if (pat[i] == ']')
            return i + 1;
    return -1;
}

static BreResult match_here(MatchContext *ctx, BreMatch *m, int *group_id, int *total_out)
{
    if (ctx->pi >= ctx->pend)
    {
        *total_out = 0;
        return BRE_OK;
    }

    if (ctx->pat[ctx->pi] == '$' && ctx->pi + 1 == ctx->pend)
    {
        if (ctx->text[ctx->ti] == '\0')
        {
            *total_out = 0;
            return BRE_OK;
        }
        return BRE_NOMATCH;
    }

    /* Group */
    {
        bool applied = false;
        int consumed = 0;
        BreResult gr = match_group(ctx, m, group_id, &consumed, &applied);
        if (gr == BRE_ERROR)
            return BRE_ERROR;
        if (applied)
        {
            *total_out = consumed;
            return BRE_OK;
        }
    }

    /* Class */
    if (ctx->pat[ctx->pi] == '[')
    {
        int atom_start = ctx->pi;
        int atom_end = find_class_end_simple(ctx->pat, atom_start, ctx->pend);
        if (atom_end < 0)
            return BRE_ERROR;
        int min_rep = 1, max_rep = 1, next_pi = atom_end;
        BreResult qr = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
        if (qr == BRE_ERROR)
            return BRE_ERROR;
        int total = 0;
        BreResult rr =
            match_repeated(ctx, once_class, atom_start, atom_end, min_rep, max_rep, next_pi, m, group_id, &total, NULL);
        if (rr != BRE_OK)
            return rr;
        *total_out = total;
        return BRE_OK;
    }

    /* Dot */
    if (ctx->pat[ctx->pi] == '.')
    {
        int atom_start = ctx->pi, atom_end = ctx->pi + 1;
        int min_rep = 1, max_rep = 1, next_pi = atom_end;
        BreResult qr = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
        if (qr == BRE_ERROR)
            return BRE_ERROR;
        int total = 0;
        BreResult rr =
            match_repeated(ctx, once_dot, atom_start, atom_end, min_rep, max_rep, next_pi, m, group_id, &total, NULL);
        if (rr != BRE_OK)
            return rr;
        *total_out = total;
        return BRE_OK;
    }

    /* Escape (non-group) */
    if (ctx->pat[ctx->pi] == '\\')
    {
        if (ctx->pi + 1 >= ctx->pend)
            return BRE_ERROR;
        char esc = ctx->pat[ctx->pi + 1];
        if (esc == '(')
            return BRE_ERROR; /* Should have matched group */
        if (esc == ')' || esc == '{' || esc == '}')
            return BRE_ERROR;
        int atom_start = ctx->pi, atom_end = ctx->pi + 2;
        int min_rep = 1, max_rep = 1, next_pi = atom_end;
        BreResult qr = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
        if (qr == BRE_ERROR)
            return BRE_ERROR;
        int total = 0;
        BreResult rr = match_repeated(ctx, once_escape, atom_start, atom_end, min_rep, max_rep, next_pi, m, group_id,
                                      &total, NULL);
        if (rr != BRE_OK)
            return rr;
        *total_out = total;
        return BRE_OK;
    }

    /* Literal */
    {
        int atom_start = ctx->pi, atom_end = ctx->pi + 1;
        int min_rep = 1, max_rep = 1, next_pi = atom_end;
        BreResult qr = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
        if (qr == BRE_ERROR)
            return BRE_ERROR;
        int total = 0;
        BreResult rr = match_repeated(ctx, once_literal, atom_start, atom_end, min_rep, max_rep, next_pi, m, group_id,
                                      &total, NULL);
        if (rr != BRE_OK)
            return rr;
        *total_out = total;
        return BRE_OK;
    }
}

/* ---------- Public API ---------- */

BreResult bre_match(const char *text, const char *pattern, BreMatch *match)
{
    if (!text || !pattern || !match)
        return BRE_ERROR;

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
        int gid = 0, consumed = 0;
        BreResult r = match_here(&ctx, match, &gid, &consumed);
        if (r == BRE_OK)
        {
            match->start = 0;
            match->length = consumed;
        }
        return r;
    }

    int tlen = (int)strlen(text);
    for (int s = 0; s <= tlen; s++)
    {
        match->num_groups = 0;
        for (int i = 0; i < BRE_MAX_GROUPS; i++)
        {
            match->groups[i].start = -1;
            match->groups[i].length = 0;
        }
        int gid = 0, consumed = 0;
        ctx.ti = s;
        BreResult r = match_here(&ctx, match, &gid, &consumed);
        if (r == BRE_ERROR)
            return BRE_ERROR;
        if (r == BRE_OK)
        {
            match->start = s;
            match->length = consumed;
            return BRE_OK;
        }
    }
    return BRE_NOMATCH;
}

char *bre_substitute(const char *text, const char *pattern, const char *replacement)
{
    if (!text || !pattern || !replacement)
        return NULL;

    BreMatch m;
    BreResult mr = bre_match(text, pattern, &m);
    if (mr == BRE_ERROR)
        return NULL;

    if (mr == BRE_NOMATCH || m.start < 0)
    {
        size_t n = strlen(text) + 1;
        char *out = (char *)malloc(n);
        if (!out)
            return NULL;
        memcpy(out, text, n);
        return out;
    }

    size_t prefix_len = (size_t)m.start;
    size_t suffix_len = strlen(text) - (size_t)m.start - (size_t)m.length;

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

    memcpy(out, text, prefix_len);
    char *p = out + prefix_len;

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

    memcpy(p, text + m.start + m.length, suffix_len);
    out[out_len] = '\0';
    return out;
}
