// Minimal BRE engine for POSIX ed: supports ., *, ^, $, bracket classes,
// basic escaped literals, and simple capture groups \( ... \) without nesting.
// Backreferences in pattern are not supported; replacement supports \1-\9.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "bre.h"

#define RE_DUP_MAX 255

typedef struct {
    const char *pat;
    int plen;
} Pat;


static int match_group_content(MatchContext *ctx, int gstart, int gend, BreMatch *m, int *group_id);
static int match_here_with_repetition(MatchContext *ctx, int atom_start, int atom_end,
                                      BreMatch *m, int *group_id, int *next_pi_out);
int match_here_group(MatchContext *ctx, BreMatch *m, int *group_id, bool direct);


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

int parse_quantifier(const char *pat, int at, int pend, int *min, int *max, int *next_pi)
{
    if (at < pend && pat[at] == '*') {
        *min = 0;
        *max = -1;
        *next_pi = at+1;
        return 1;
    }
    if (at+1 < pend && pat[at] == '\\' && pat[at+1] == '+') {
        *min = 1;
        *max = -1;
        *next_pi = at+2;
        return 1;
    }
    return parse_bre_repetition(pat, at, pend, min, max, next_pi);
    // existing
}

    // Forward declaration
static int match_here(MatchContext *ctx, BreMatch *m, int *group_id);
static int inner_once(MatchContext *ctx, int tpos, int inner_start, int inner_end, BreMatch *m, int gid_orig);


// Helper functions follow the convention:
// return >0: matched this atom and consumed chars (plus rest)
// return 0 : this atom does not apply at pat[pi] (not found)
// return -1: syntax/error or mismatch for applicable atom
// Group matcher using parse_quantifier() and precise backtracking
int match_here_group(MatchContext *ctx, BreMatch *m, int *group_id, bool direct)
{
    // Recognize \( ... \)
    if (!(ctx->pat[ctx->pi] == '\\' &&
          ctx->pi + 1 < ctx->pend &&
          ctx->pat[ctx->pi + 1] == '(')) {
        return 0; // not a group at this position
    }

    // Find matching \)
    int gend = find_group_end(ctx->pat, ctx->pi, ctx->pend);
    if (gend < 0) return -1; // malformed

    // Outer group numbering (do not commit until success)
    int gid_orig = *group_id;
    int group_no = gid_orig + 1;        // 1..9
    int gidx     = group_no - 1;

    // Atom bounds for the group and inner content
    int atom_start  = ctx->pi;          // position of '\'
    int atom_end    = gend + 2;         // just after "\)"
    int inner_start = ctx->pi + 2;      // after "\("
    int inner_end   = gend;             // at the '\'

    // Direct mode: match inner exactly once (no rest/quantifier handling here).
    // The caller will handle the rest. We only return the length of one occurrence.
    if (direct) {
        MatchContext inner_ctx = *ctx;
        inner_ctx.pi   = inner_start;
        inner_ctx.pend = inner_end;

        int gid_tmp = gid_orig; // inner groups unsupported; keep stable
        int adv = match_here(&inner_ctx, m, &gid_tmp);
        if (adv < 0) return -1;

        // Record this occurrence for the outer group
        m->groups[gidx].start  = ctx->ti;
        m->groups[gidx].length = adv;
        if (group_no > m->num_groups) m->num_groups = group_no;

        *group_id = group_no; // commit this group number
        return adv;
    }

    // Parse optional quantifier following the group
    int min_rep = 1, max_rep = 1, next_pi = atom_end;
    int qres = parse_quantifier(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);
    if (qres < 0) return -1; // malformed quantifier

    // No quantifier: match inner once, then rest. Record capture only if rest succeeds.
    if (qres == 0) {
        MatchContext inner_ctx = *ctx;
        inner_ctx.ti   = ctx->ti;
        inner_ctx.pi   = inner_start;
        inner_ctx.pend = inner_end;

        int gid_tmp = gid_orig; // keep outer numbering stable during speculative inner matches
        int adv = match_here(&inner_ctx, m, &gid_tmp);
        if (adv < 0) return -1;

        MatchContext rest_ctx = *ctx;
        rest_ctx.ti = ctx->ti + adv;
        rest_ctx.pi = atom_end;

        int gid_tmp_rest = gid_orig + 1; // consume the group number
        int rest = match_here(&rest_ctx, m, &gid_tmp_rest);
        if (rest < 0) return -1;

        // Success: record capture and commit numbering
        m->groups[gidx].start  = ctx->ti;
        m->groups[gidx].length = adv;
        if (group_no > m->num_groups) m->num_groups = group_no;

        *group_id = gid_tmp_rest;
        return adv + rest;
    }

    // With quantifier: greedy repetition of inner, precise backtracking
    int tpos = ctx->ti;
    int max_possible = (max_rep < 0) ? 10000 : max_rep;

    // Store each occurrence's adv to backtrack exactly
    // Use a small fixed-capacity stack to avoid dynamic allocation
    int advs[256];
    int adv_count = 0;

    while (adv_count < max_possible) {
        MatchContext inner_ctx = *ctx;
        inner_ctx.ti   = tpos;
        inner_ctx.pi   = inner_start;
        inner_ctx.pend = inner_end;

        int gid_tmp = gid_orig; // keep outer numbering stable during speculative inner matches
        int adv = match_here(&inner_ctx, m, &gid_tmp);
        if (adv < 0) break;
        if (adv_count < (int)(sizeof(advs) / sizeof(advs[0]))) {
            advs[adv_count] = adv;
        } else {
            // If we overflow, stop greedily (protect against runaway)
            break;
        }
        adv_count++;
        tpos += adv;
    }

    // Backtrack from greedy count down to min_rep
    int have = adv_count;
    while (have >= min_rep) {
        MatchContext rest_ctx = *ctx;
        rest_ctx.ti = tpos;
        rest_ctx.pi = next_pi;

        int gid_tmp_rest = gid_orig + 1; // consume the outer group number
        int rest = match_here(&rest_ctx, m, &gid_tmp_rest);
        if (rest >= 0) {
            // Record the full span (all repetitions matched)
            m->groups[gidx].start  = ctx->ti;
            m->groups[gidx].length = tpos - ctx->ti;
            if (group_no > m->num_groups) m->num_groups = group_no;

            *group_id = gid_tmp_rest;
            return (tpos - ctx->ti) + rest;
        }

        // Pop one occurrence and back off exactly by its adv
        if (have > 0) {
            int last_adv = (have <= (int)(sizeof(advs) / sizeof(advs[0]))) ? advs[have - 1] : 1;
            tpos -= last_adv;
            have--;
        } else {
            break;
        }
    }

    return -1;
}

static int match_here_class(MatchContext *ctx, BreMatch *m, int *group_id, bool direct)
{
    if (ctx->pat[ctx->pi] != '[') return 0;  // not a class

    int after = 0;
    if (ctx->text[ctx->ti] == '\0') return -1;
    if (!in_char_class((unsigned char)ctx->text[ctx->ti], ctx->pat, ctx->pi, ctx->pend, &after))
        return -1;

    if (direct) {
        // Direct match: just match one character in the class
        return 1;
    }

    // Full match with recursion and repetition
    int atom_start = ctx->pi;
    int atom_end   = after;

    // Now let the repetition handler deal with possible \{n,m\} or *
    int next_pi;
    int consumed;

    if (after < ctx->pend && ctx->pat[after] == '*') {
        // Handle legacy * repetition (keep your existing logic)
        int tcur = ctx->ti;
        int after_local = 0;
        while (ctx->text[tcur] && in_char_class((unsigned char)ctx->text[tcur], ctx->pat, ctx->pi, ctx->pend, &after_local))
            tcur++;

        int star_next_pi = after + 1;
        for (int ttry = tcur; ttry >= ctx->ti; ttry--) {
            MatchContext rest_ctx = *ctx;
            rest_ctx.ti = ttry;
            rest_ctx.pi = star_next_pi;
            int rest = match_here(&rest_ctx, m, group_id);
            if (rest >= 0)
                return (ttry - ctx->ti) + rest;
        }
        return -1;
    }
    else {
        // No * → check for \{n,m\} repetition
        MatchContext rep_ctx = *ctx;
        rep_ctx.pi = atom_start;
        consumed = match_here_with_repetition(&rep_ctx, atom_start, atom_end, m, group_id, &next_pi);
        if (consumed < 0) return -1;

        MatchContext rest_ctx = *ctx;
        rest_ctx.ti = ctx->ti + consumed;
        rest_ctx.pi = next_pi;
        int rest = match_here(&rest_ctx, m, group_id);
        if (rest < 0) return -1;
        return consumed + rest;
    }
}

static int match_here_dot(MatchContext *ctx, BreMatch *m, int *group_id, bool direct)
{
    if (ctx->pat[ctx->pi] != '.') return 0;  // not a dot

    if (ctx->text[ctx->ti] == '\0') return -1;

    if (direct) {
        // Direct match: just match any single character
        return 1;
    }

    // Full match with recursion and repetition
    int atom_start = ctx->pi;
    int atom_end   = ctx->pi + 1;   // '.' is always one char in pattern

    // Special case: .* (legacy BRE zero-or-more)
    if (ctx->pi + 1 < ctx->pend && ctx->pat[ctx->pi + 1] == '*') {
        // Keep your original working .* logic
        int tcur = ctx->ti;
        while (ctx->text[tcur]) tcur++;                 // match as much as possible
        int next_pi = ctx->pi + 2;

        for (int ttry = tcur; ttry >= ctx->ti; ttry--) {
            MatchContext rest_ctx = *ctx;
            rest_ctx.ti = ttry;
            rest_ctx.pi = next_pi;
            int rest = match_here(&rest_ctx, m, group_id);
            if (rest >= 0)
                return (ttry - ctx->ti) + rest;
        }
        return -1;
    }

    // Otherwise: normal . with possible \{n,m\} repetition
    int next_pi;
    MatchContext rep_ctx = *ctx;
    rep_ctx.pi = atom_start;
    int consumed = match_here_with_repetition(&rep_ctx, atom_start, atom_end, m, group_id, &next_pi);
    if (consumed < 0) return -1;

    MatchContext rest_ctx = *ctx;
    rest_ctx.ti = ctx->ti + consumed;
    rest_ctx.pi = next_pi;
    int rest = match_here(&rest_ctx, m, group_id);
    if (rest < 0) return -1;

    return consumed + rest;
}

static int match_here_escape(MatchContext *ctx, BreMatch *m, int *group_id, bool direct)
{
    if (!(ctx->pat[ctx->pi] == '\\' && ctx->pi + 1 < ctx->pend)) return 0;  // not an escape sequence

    char esc = ctx->pat[ctx->pi + 1];

    // These are invalid or handled elsewhere:
    if (esc == '(' || esc == ')' || esc == '{' || esc == '}') {
        return 0;  // Let group or repetition logic handle these
    }

    if (ctx->text[ctx->ti] == '\0' || ctx->text[ctx->ti] != esc) return -1;

    if (direct) {
        // Direct match: just match the escaped character
        return 1;
    }

    // Full match with recursion and repetition
    int atom_start = ctx->pi;
    int atom_end   = ctx->pi + 2;   // \X is always two chars in pattern

    // Special case: escaped char followed by * → e.g. \.* or \n*
    if (ctx->pi + 2 < ctx->pend && ctx->pat[ctx->pi + 2] == '*') {
        int tcur = ctx->ti;
        while (ctx->text[tcur] == esc) tcur++;
        int next_pi = ctx->pi + 3;

        for (int ttry = tcur; ttry >= ctx->ti; ttry--) {
            MatchContext rest_ctx = *ctx;
            rest_ctx.ti = ttry;
            rest_ctx.pi = next_pi;
            int rest = match_here(&rest_ctx, m, group_id);
            if (rest >= 0)
                return (ttry - ctx->ti) + rest;
        }
        return -1;
    }

    // Otherwise: normal escaped literal with possible \{n,m\}
    int next_pi;
    MatchContext rep_ctx = *ctx;
    rep_ctx.pi = atom_start;
    int consumed = match_here_with_repetition(&rep_ctx, atom_start, atom_end, m, group_id, &next_pi);
    if (consumed < 0) return -1;

    MatchContext rest_ctx = *ctx;
    rest_ctx.ti = ctx->ti + consumed;
    rest_ctx.pi = next_pi;
    int rest = match_here(&rest_ctx, m, group_id);
    if (rest < 0) return -1;

    return consumed + rest;
}


static int match_here_literal(MatchContext *ctx, BreMatch *m, int *group_id, bool direct)
{
    // Not a literal if it starts with a special char
    if (ctx->pi >= ctx->pend) return 0;
    if (ctx->pat[ctx->pi] == '\\' || ctx->pat[ctx->pi] == '[' || ctx->pat[ctx->pi] == '.' ||
        ctx->pat[ctx->pi] == '^' || ctx->pat[ctx->pi] == '$' || ctx->pat[ctx->pi] == '*') {
        return 0;
    }

    if (ctx->text[ctx->ti] == '\0' || ctx->text[ctx->ti] != ctx->pat[ctx->pi]) return -1;

    if (direct) {
        // Direct match: just match the literal character
        return 1;
    }

    // Full match with recursion and repetition
    
    int atom_start = ctx->pi;
    int atom_end   = ctx->pi + 1;   // single literal char

    // Special case: literal followed by * → e.g. a*, x*
    if (ctx->pi + 1 < ctx->pend && ctx->pat[ctx->pi + 1] == '*') {
        int tcur = ctx->ti;
        while (ctx->text[tcur] == ctx->pat[ctx->pi]) tcur++;
        int next_pi = ctx->pi + 2;

        for (int ttry = tcur; ttry >= ctx->ti; ttry--) {
            MatchContext rest_ctx = *ctx;
            rest_ctx.ti = ttry;
            rest_ctx.pi = next_pi;
            int rest = match_here(&rest_ctx, m, group_id);
            if (rest >= 0)
                return (ttry - ctx->ti) + rest;
        }
        return -1;
    }

    // Otherwise: normal literal with possible \{n,m\}
    int next_pi;
    MatchContext rep_ctx = *ctx;
    rep_ctx.pi = atom_start;
    int consumed = match_here_with_repetition(&rep_ctx, atom_start, atom_end, m, group_id, &next_pi);
    if (consumed < 0) return -1;

    MatchContext rest_ctx = *ctx;
    rest_ctx.ti = ctx->ti + consumed;
    rest_ctx.pi = next_pi;
    int rest = match_here(&rest_ctx, m, group_id);
    if (rest < 0) return -1;

    return consumed + rest;
}

// Returns number of characters consumed in text, or -1 on failure
static int match_group_content(MatchContext *ctx, int gstart, int gend, BreMatch *m, int *group_id) {
    MatchContext group_ctx = *ctx;
    group_ctx.pi = gstart;
    group_ctx.pend = gend;
    return match_here(&group_ctx, m, group_id);
}

/* --------------------------------------------------------------
   BRE repetition parser:  \{n\}   \{n,\}   \{n,m\}
   Returns:
       0 = no repetition (not starting with \{)
       1 = valid repetition found
      -1 = malformed (unterminated, bad syntax, etc.)
   If valid, fills *min_rep, *max_rep, and *next_pi
   -------------------------------------------------------------- */
int parse_bre_repetition(const char *pat, int pi, int pend,
			 int *min_rep, int *max_rep, int *next_pi)
{
    if (pi + 5 > pend) {
	// too short for \{n\}
	if (pat[pi] == '\\' && pi+1 < pend &&  pat[pi+1] == '{')
	    // Open brace without close brace.
	    return -1;
	// No open brace.
	return 0;		
    }
    if (pat[pi] != '\\' || pat[pi+1] != '{')
	return 0;

    int j = pi + 2;		// first char after \{
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

    if (pat[j] == '\\' && j + 1 < pend && pat[j+1] == '}') {
        // \{n\}
        *max_rep = *min_rep;
        *next_pi = j + 2;
        return 1;
    }

    if (pat[j] != ',')
	return -1;		// garbage
    j++;			// skip comma

    if (j >= pend)
	return -1;

    if (pat[j] == '\\' && j + 1 < pend && pat[j+1] =='}') {
        // \{n,\} -> n or more
        *max_rep = -1;                               // unbounded
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

    
    if (j + 1 >= pend || pat[j] != '\\' || pat[j+1] != '}')
	return -1;

    *next_pi = j + 2;
    return 1;
}


static int match_here(MatchContext *ctx, BreMatch *m, int *group_id) {
    if (ctx->pi >= ctx->pend) return 0; // matched to end of pattern

    // End anchor at end of pattern
    if (ctx->pat[ctx->pi] == '$' && ctx->pi + 1 == ctx->pend) {
        return ctx->text[ctx->ti] == '\0' ? 0 : -1;
    }
    // Try each atom in order; if none applies, treat as mismatch
    int ret;
    ret = match_here_group(ctx, m, group_id, false); if (ret != 0) return ret;
    ret = match_here_class(ctx, m, group_id, false); if (ret != 0) return ret;
    ret = match_here_dot(ctx, m, group_id, false);   if (ret != 0) return ret;
    ret = match_here_escape(ctx, m, group_id, false);if (ret != 0) return ret;
    ret = match_here_literal(ctx, m, group_id, false);if (ret != 0) return ret;
    return -1;

 }

 
 /* Match one instance of an atom directly — no recursion into match_here */
static int match_atom_directly(MatchContext *ctx, BreMatch *m, int *group_id)
{
    if (ctx->pi >= ctx->pend) return -1;

    // Try each atom type directly using the helper functions
    int ret;
    ret = match_here_group(ctx, m, group_id, true); if (ret != 0) return ret;
    ret = match_here_class(ctx, m, group_id, true); if (ret != 0) return ret;
    ret = match_here_dot(ctx, m, group_id, true);   if (ret != 0) return ret;
    ret = match_here_escape(ctx, m, group_id, true);if (ret != 0) return ret;
    ret = match_here_literal(ctx, m, group_id, true);if (ret != 0) return ret;
    return -1;
}

static int match_here_with_repetition(MatchContext *ctx, int atom_start, int atom_end,
                                      BreMatch *m, int *group_id, int *next_pi_out)
{
    int min_rep = 1, max_rep = 1;
    int next_pi = atom_end;

    // First try \{n,m\}
    int rep_res = parse_bre_repetition(ctx->pat, atom_end, ctx->pend, &min_rep, &max_rep, &next_pi);

    // Then try \+ (one-or-more) as a repetition operator
    if (rep_res != 1) {
        if (atom_end + 1 <= ctx->pend - 1 &&
            ctx->pat[atom_end] == '\\' && ctx->pat[atom_end + 1] == '+') {
            min_rep = 1;
            max_rep = -1;        // unbounded
            next_pi = atom_end + 2;
            rep_res = 1;
        }
    }

    if (rep_res != 1) {
        // No repetition → match once directly
        MatchContext atom_ctx = *ctx;
        atom_ctx.pi = atom_start;
        int adv = match_atom_directly(&atom_ctx, m, group_id);
        if (adv <= 0) return -1;
        *next_pi_out = atom_end;
        return adv;
    }

    // We have repetition (either \{n,m\} or \+)
    int max_possible = (max_rep < 0) ? 10000 : max_rep;
    int tpos = ctx->ti;
    int count = 0;

    // Greedy match as many as possible
    while (count < max_possible) {
        MatchContext atom_ctx = *ctx;
        atom_ctx.ti = tpos;
        atom_ctx.pi = atom_start;
        int adv = match_atom_directly(&atom_ctx, m, group_id);
        if (adv <= 0) break;
        tpos += adv;
        count++;
    }

    // Backtrack and try to match the rest
    while (count >= min_rep) {
        MatchContext rest_ctx = *ctx;
        rest_ctx.ti = tpos;
        rest_ctx.pi = next_pi;
        int rest = match_here(&rest_ctx, m, group_id);
        if (rest >= 0) {
            *next_pi_out = next_pi;
            return (tpos - ctx->ti) + rest;
        }
        // Back off one character conservatively
        if (count-- > 0) {
            tpos--;
            while (tpos > ctx->ti && (ctx->text[tpos] == '\0' || ctx->text[tpos] == '\n')) tpos--;
        }
    }
    return -1;
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

    MatchContext ctx = {
        .base = text,
        .text = text,
        .ti = 0,
        .pat = pattern,
        .pi = start_pi,
        .pend = plen
    };

    if (anchored_start) {
        int gid = 0;
        int consumed = match_here(&ctx, match, &gid);
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
        ctx.ti = s;
        int consumed = match_here(&ctx, match, &gid);
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
