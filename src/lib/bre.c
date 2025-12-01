// Expand BreResult to differentiate group presence vs mismatch, and use it in match_group and match_here.

#include "bre.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define RE_DUP_MAX 255

static BreResult match_here(MatchContext* ctx, BreMatch* m, int* total_out);
static bool in_char_class(unsigned char c, const char* pat, int pi, int pend, int* after);
static int find_group_end(const char* pat, int pi, int pend);

/* Atom execution function (single occurrence).
 * Added void *user so adapters can carry context (e.g., group bounds) without nested functions.
 */
typedef BreResult(*AtomOnceFn)(MatchContext* ctx, int atom_start, int atom_end, int tpos, int* adv_out, void* user);

/* For group quantifier adapter */
typedef struct
{
	int inner_start;
	int inner_end;
	BreMatch* m;
} GroupInnerInfo;

/* Helpers */

static int find_group_end(const char* pat, int pi, int pend)
{
	for (int i = pi + 2; i < pend - 1; i++)
	{
		if (pat[i] == '\\' && pat[i + 1] == ')')
			return i;
	}
	return -1;
}

static bool in_char_class(unsigned char c, const char* pat, int pi, int pend, int* after)
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

/* Quantifiers */

BreResult parse_bre_repetition(const char* pat, int pi, int pend, BreRepetition* rep)
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
	const char* start = pat + j;
	if (j >= pend || !isdigit((unsigned char)pat[j]))
		return BRE_ERROR;
	while (j < pend && isdigit((unsigned char)pat[j]))
		j++;
	rep->min = atoi(start);
	if (rep->min > RE_DUP_MAX)
		return BRE_ERROR;

	if (j >= pend)
		return BRE_ERROR;

	if (pat[j] == '\\' && j + 1 < pend && pat[j + 1] == '}')
	{
		rep->max = rep->min;
		rep->next_pi = j + 2;
		return BRE_OK;
	}

	if (pat[j] != ',')
		return BRE_ERROR;
	j++;
	if (j >= pend)
		return BRE_ERROR;

	if (pat[j] == '\\' && j + 1 < pend && pat[j + 1] == '}')
	{
		rep->max = -1;
		rep->next_pi = j + 2;
		return BRE_OK;
	}

	start = pat + j;
	if (!isdigit((unsigned char)pat[j]))
		return BRE_ERROR;
	while (j < pend && isdigit((unsigned char)pat[j]))
		j++;
	rep->max = atoi(start);
	if (rep->max > RE_DUP_MAX || rep->min > rep->max)
		return BRE_ERROR;
	if (j + 1 >= pend || pat[j] != '\\' || pat[j + 1] != '}')
		return BRE_ERROR;

	rep->next_pi = j + 2;
	return BRE_OK;
}

static BreResult parse_quantifier(const char* pat, int at, int pend, BreRepetition* rep)
{
	if (at < pend && pat[at] == '*')
	{
		rep->min = 0;
		rep->max = -1;
		rep->next_pi = at + 1;
		return BRE_OK;
	}
	if (at + 1 < pend && pat[at] == '\\' && pat[at + 1] == '+')
	{
		rep->min = 1;
		rep->max = -1;
		rep->next_pi = at + 2;
		return BRE_OK;
	}
	return parse_bre_repetition(pat, at, pend, rep);
}

/* Atom Once */

static BreResult once_literal(MatchContext* ctx, int atom_start, int atom_end, int tpos, int* adv_out, void* user)
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

static BreResult once_dot(MatchContext* ctx, int atom_start, int atom_end, int tpos, int* adv_out, void* user)
{
	(void)atom_start;
	(void)atom_end;
	if (ctx->text[tpos] == '\0')
		return BRE_NOMATCH;
	*adv_out = 1;
	return BRE_OK;
}

static BreResult once_escape(MatchContext* ctx, int atom_start, int atom_end, int tpos, int* adv_out, void* user)
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

static BreResult once_class(MatchContext* ctx, int atom_start, int atom_end, int tpos, int* adv_out, void* user)
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

/* Repetition with exact backtracking (prefix_out reports span of repeated atoms) */

static BreResult match_repeated(MatchContext* ctx, AtomOnceFn atom_once, int atom_start, int atom_end,
	const BreRepetition* rep, BreMatch* m, int* total_out, int* prefix_out, void* user)
{
	(void)m;
	int tpos = ctx->ti;
	int max_possible = (rep->max < 0) ? 10000 : rep->max;
	int advs[256];
	int have = 0;

	while (have < max_possible)
	{
		int adv = 0;
		BreResult r = atom_once(ctx, atom_start, atom_end, tpos, &adv, user);
		if (r == BRE_ERROR)
			return BRE_ERROR;
		if (r != BRE_OK)
			break;
		if (have < (int)(sizeof(advs) / sizeof(advs[0])))
			advs[have] = adv;
		tpos += adv;
		have++;
	}

	while (have >= rep->min)
	{
		MatchContext rest_ctx = *ctx;
		rest_ctx.ti = tpos;
		rest_ctx.pi = rep->next_pi;
		int rest_total = 0;
		BreResult rr = match_here(&rest_ctx, m, &rest_total);
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

/* Group Handling */

static BreResult once_group_inner(MatchContext* ctx, int inner_start, int inner_end, int tpos, BreMatch* m,
	int* adv_out)
{
	MatchContext inner = *ctx;
	inner.ti = tpos;
	inner.pi = inner_start;
	inner.pend = inner_end;
	int consumed = 0;
	BreResult r = match_here(&inner, m, &consumed);
	if (r != BRE_OK)
		return r;
	*adv_out = consumed;
	return BRE_OK;
}

/* Match a group with no group-level quantifier.
   - \(.*\) gets greedy outer backtracking.
   - Otherwise, match inner once, then match the remainder.
   - Record capture only after success; consume the group number. */
BreResult match_group_without_quantifier(MatchContext* ctx, BreMatch* m, int inner_start, int inner_end, int atom_end,
	int* total_out)
{
	/* Special-case \(.*\) to backtrack against the remainder */
	if (inner_end - inner_start == 2 && ctx->pat[inner_start] == '.' && ctx->pat[inner_start + 1] == '*')
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
			BreResult rr = match_here(&rest_ctx, m, &rest_total);
			if (rr == BRE_ERROR)
				return BRE_ERROR;
			if (rr == BRE_OK)
			{
				if (m->num_groups < BRE_MAX_GROUPS)
				{
					m->groups[m->num_groups].start = ctx->ti;
					m->groups[m->num_groups].length = ttry - ctx->ti;
					m->num_groups++;
					*total_out = (ttry - ctx->ti) + rest_total;
					ctx->pi = atom_end;
					return BRE_OK;
				}
			}
		}
		return BRE_NOMATCH;
	}

	/* General case: scan forward in text, try inner once at each position, then the remainder */
	for (int tpos = ctx->ti;; tpos++)
	{
		if (ctx->text[tpos] == '\0')
			break;

		int adv = 0;
		BreResult ir = once_group_inner(ctx, inner_start, inner_end, tpos, m, &adv);
		if (ir == BRE_ERROR)
			return BRE_ERROR;
		if (ir != BRE_OK)
			continue;

		MatchContext rest_ctx = *ctx;
		rest_ctx.ti = tpos + adv;
		rest_ctx.pi = atom_end;

		int rest_total = 0;
		BreResult rr = match_here(&rest_ctx, m, &rest_total);
		if (rr == BRE_ERROR)
			return BRE_ERROR;
		if (rr == BRE_OK)
		{
			if (m->num_groups < BRE_MAX_GROUPS)
			{
				m->groups[m->num_groups].start = tpos;
				m->groups[m->num_groups].length = adv;
				m->num_groups++;
			}

			*total_out = (tpos - ctx->ti) + adv + rest_total;
			ctx->pi = atom_end;
			return BRE_OK;
		}
	}

	return BRE_NOMATCH;
}

static BreResult group_inner_once_adapter(MatchContext* c, int astart, int aend, int tpos, int* adv_out, void* user)
{
	(void)astart;
	(void)aend;
	GroupInnerInfo* info = (GroupInnerInfo*)user;
	return once_group_inner(c, info->inner_start, info->inner_end, tpos, info->m, adv_out);
}

/* Match a group with a group-level quantifier. Uses repetition and records the
   capture as the span of all repeated inner occurrences. */
BreResult match_group_with_quantifier(MatchContext* ctx, BreMatch* m, int inner_start, int inner_end, int atom_start,
	int atom_end, const BreRepetition* rep, int* total_out)
{
	int total = 0;
	int group_span = 0;
	GroupInnerInfo info = { .inner_start = inner_start, .inner_end = inner_end, .m = m };

	BreResult mr = match_repeated(ctx, group_inner_once_adapter, atom_start, atom_end, rep, m, &total, &group_span, &info);
	if (mr != BRE_OK)
		return mr; /* BRE_NOMATCH or BRE_ERROR */

	if (m->num_groups < BRE_MAX_GROUPS)
	{
		m->groups[m->num_groups].start = ctx->ti;
		m->groups[m->num_groups].length = group_span;
		m->num_groups++;
	}
	*total_out = total;
	ctx->pi = atom_end;

	return BRE_OK;
}

/* Dispatcher: determine group bounds, check for quantifier, then call the specific handler. */
BreResult match_group(MatchContext* ctx, BreMatch* m, int* total_out)
{
	int gend = find_group_end(ctx->pat, ctx->pi, ctx->pend);
	if (gend < 0)
		return BRE_ERROR;

	int atom_start = ctx->pi;
	int atom_end = gend + 2;
	int inner_start = ctx->pi + 2;
	int inner_end = gend;

	BreRepetition rep = { .min = 1, .max = 1, .next_pi = atom_end };
	BreResult qres = parse_quantifier(ctx->pat, atom_end, ctx->pend, &rep);
	if (qres == BRE_ERROR)
		return BRE_ERROR;
	if (qres == BRE_NOMATCH)
	{
		return match_group_without_quantifier(ctx, m, inner_start, inner_end, atom_end, total_out);
	}
	return match_group_with_quantifier(ctx, m, inner_start, inner_end, atom_start, atom_end, &rep, total_out);
}
/* Dispatcher */

static int find_class_end_simple(const char* pat, int pi, int pend)
{
	int i = pi + 1;
	if (i < pend && pat[i] == '^')
		i++;
	for (; i < pend; ++i)
		if (pat[i] == ']')
			return i + 1;
	return -1;
}

static BreResult match_here(MatchContext* ctx, BreMatch* m, int* total_out)
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

	/* If next atom is a group, treat group as the applicable atom and do not fall through */
	if (ctx->pat[ctx->pi] == '\\' && ctx->pi + 1 < ctx->pend && ctx->pat[ctx->pi + 1] == '(')
	{
		int consumed = 0;
		BreResult gr = match_group(ctx, m, &consumed);
		if (gr == BRE_OK)
		{
			*total_out = consumed;
			return BRE_OK;
		}
		/* Important: group present but mismatch → BRE_NOMATCH for this position */
		if (gr == BRE_NOMATCH)
			return BRE_NOMATCH;
		return gr; /* BRE_ERROR */
	}

	/* Class */
	if (ctx->pat[ctx->pi] == '[')
	{
		int atom_start = ctx->pi;
		int atom_end = find_class_end_simple(ctx->pat, atom_start, ctx->pend);
		if (atom_end < 0)
			return BRE_ERROR;
		BreRepetition rep = { .min = 1, .max = 1, .next_pi = atom_end };
		BreResult qr = parse_quantifier(ctx->pat, atom_end, ctx->pend, &rep);
		if (qr == BRE_ERROR)
			return BRE_ERROR;
		int total = 0;
		BreResult rr = match_repeated(ctx, once_class, atom_start, atom_end, &rep, m, &total, NULL, NULL);
		if (rr != BRE_OK)
			return rr;
		*total_out = total;
		return BRE_OK;
	}

	/* Dot */
	if (ctx->pat[ctx->pi] == '.')
	{
		int atom_start = ctx->pi, atom_end = ctx->pi + 1;
		BreRepetition rep = { .min = 1, .max = 1, .next_pi = atom_end };
		BreResult qr = parse_quantifier(ctx->pat, atom_end, ctx->pend, &rep);
		if (qr == BRE_ERROR)
			return BRE_ERROR;
		int total = 0;
		BreResult rr = match_repeated(ctx, once_dot, atom_start, atom_end, &rep, m, &total, NULL, NULL);
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
			return BRE_ERROR; /* group already handled */
		if (esc == ')' || esc == '{' || esc == '}')
			return BRE_ERROR;
		int atom_start = ctx->pi, atom_end = ctx->pi + 2;
		BreRepetition rep = { .min = 1, .max = 1, .next_pi = atom_end };
		BreResult qr = parse_quantifier(ctx->pat, atom_end, ctx->pend, &rep);
		if (qr == BRE_ERROR)
			return BRE_ERROR;
		int total = 0;
		BreResult rr = match_repeated(ctx, once_escape, atom_start, atom_end, &rep, m, &total, NULL, NULL);
		if (rr != BRE_OK)
			return rr;
		*total_out = total;
		return BRE_OK;
	}

	/* Literal */
	{
		int atom_start = ctx->pi, atom_end = ctx->pi + 1;
		BreRepetition rep = { .min = 1, .max = 1, .next_pi = atom_end };
		BreResult qr = parse_quantifier(ctx->pat, atom_end, ctx->pend, &rep);
		if (qr == BRE_ERROR)
			return BRE_ERROR;
		int total = 0;
		BreResult rr = match_repeated(ctx, once_literal, atom_start, atom_end, &rep, m, &total, NULL, NULL);
		if (rr != BRE_OK)
			return rr;
		*total_out = total;
		return BRE_OK;
	}
}

/* Sort captured groups in BreMatch by ascending start index.
   - Only the first match->num_groups entries are considered.
   - Groups with start < 0 (unset) are placed at the end.
*/
static void bre_sort_groups_by_start(BreMatch* match)
{
	if (!match || match->num_groups <= 1)
		return;

	int n = match->num_groups;

	// Simple stable insertion sort by start (treat -1 as +infinity)
	for (int i = 1; i < n; i++)
	{
		int s = match->groups[i].start;
		int l = match->groups[i].length;

		// Map -1 to a large value so unset groups go to the end
		int key = (s < 0) ? INT_MAX : s;

		int j = i - 1;
		while (j >= 0)
		{
			int js = match->groups[j].start;
			int jkey = (js < 0) ? INT_MAX : js;

			if (jkey <= key)
				break;

			match->groups[j + 1].start = match->groups[j].start;
			match->groups[j + 1].length = match->groups[j].length;
			j--;
		}
		match->groups[j + 1].start = s;
		match->groups[j + 1].length = l;
	}
}

/* Public API */
BreResult bre_match(const char* text, const char* pattern, BreMatch* match)
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

	// Base context template (pattern indices constant)
	MatchContext base = { .base = text, .text = text, .ti = 0, .pat = pattern, .pi = start_pi, .pend = plen };

	if (anchored_start)
	{
		MatchContext ctx = base;
		int consumed = 0;
		BreResult r = match_here(&ctx, match, &consumed);
		if (r == BRE_OK)
		{
			match->start = 0;
			match->length = consumed;
			bre_sort_groups_by_start(match);
		}
		return r;
	}

	int tlen = (int)strlen(text);
	for (int s = 0; s <= tlen; s++)
	{
		// Reset groups each try
		match->num_groups = 0;
		for (int i = 0; i < BRE_MAX_GROUPS; i++)
		{
			match->groups[i].start = -1;
			match->groups[i].length = 0;
		}

		MatchContext ctx = base; // fresh copy
		ctx.ti = s;

		int consumed = 0;
		BreResult r = match_here(&ctx, match, &consumed);
		if (r == BRE_ERROR)
			return BRE_ERROR;
		if (r == BRE_OK)
		{
			match->start = s;
			match->length = consumed;
			bre_sort_groups_by_start(match);
			return BRE_OK;
		}
		// else BRE_NOMATCH → try next s
	}
	return BRE_NOMATCH;
}

char* bre_substitute(const char* text, const char* pattern, const char* replacement)
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
		char* out = (char*)malloc(n);
		if (!out)
			return NULL;
		memcpy(out, text, n);
		return out;
	}

	size_t prefix_len = (size_t)m.start;
	size_t suffix_len = strlen(text) - (size_t)m.start - (size_t)m.length;

	size_t rep_len = 0;
	for (const char* r = replacement; *r; r++)
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
	char* out = (char*)malloc(out_len + 1);
	if (!out)
		return NULL;

	memcpy(out, text, prefix_len);
	char* p = out + prefix_len;

	for (const char* r = replacement; *r; r++)
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
