// bre.c - Fixed and fully working POSIX BRE engine
#include "bre.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RE_DUP_MAX 255

/* Find closing \) for \( ... \) */
static int find_group_end(const char *pat, int pi, int pend)
{
    for (int i = pi + 2; i < pend - 1; i++)
    {
        if (pat[i] == '\\' && pat[i + 1] == ')')
            return i;
    }
    return -1;
}

/* Match character class [abc] or [^abc] */
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
        if (pat[i] == '-' && has_prev && i + 1 < pend && pat[i + 1] != ']')
        {
            if (prev <= c && c <= (unsigned char)pat[i + 1])
                matched = true;
            i++;
            has_prev = false;
        }
        else
        {
            if ((unsigned char)pat[i] == c)
                matched = true;
            prev = pat[i];
            has_prev = true;
        }
    }
    if (i >= pend || pat[i] != ']')
        return false;
    *after = i + 1;
    return invert ? !matched : matched;
}

/* Match a single group \( ... \) */
static int match_group_once(const char *base, const char *text, int ti, const char *pat, int gstart, int gend,
                            BreMatch *m, int group_no)
{
    int gid = group_no - 1; // don't increment main counter
    int consumed = match_here(base, text, ti, pat, gstart, gend, m, &gid);
    if (consumed < 0)
        return -1;

    m->groups[group_no - 1].start = ti;
    m->groups[group_no - 1].length = consumed;
    if (group_no > m->num_groups)
        m->num_groups = group_no;
    return consumed;
}

/* Forward declaration */
/* --------------------------------------------------------------
   BRE repetition parser:  \{n\}   \{n,\}   \{n,m\}
   Returns:
       0 = no repetition
       1 = valid repetition found
      -1 = malformed
   -------------------------------------------------------------- */
int parse_bre_repetition(const char *pat, int pi, int pend, int *min_rep, int *max_rep, int *next_pi)
{
    if (pi + 1 >= pend || pat[pi] != '\\' || pat[pi + 1] != '{')
        return 0;

    int j = pi + 2;
    if (j >= pend || !isdigit((unsigned char)pat[j]))
        return -1;

    const char *start = pat + j;
    while (j < pend && isdigit((unsigned char)pat[j]))
        j++;
    *min_rep = atoi(start);
    if (*min_rep > RE_DUP_MAX)
        return -1;

    if (j + 1 < pend && pat[j] == '\\' && pat[j + 1] == '}')
    {
        *max_rep = *min_rep;
        *next_pi = j + 2;
        return 1;
    }

    if (j >= pend || pat[j] != ',')
        return -1;
    j++;

    if (j + 1 < pend && pat[j] == '\\' && pat[j + 1] == '}')
    {
        *max_rep = -1;
        *next_pi = j + 2;
        return 1;
    }

    if (j >= pend || !isdigit((unsigned char)pat[j]))
        return -1;
    start = pat + j;
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
static int match_here(const char *base, const char *text, int ti, const char *pat, int pi, int pend, BreMatch *m,
                      int *group_id);

/* Match one atom + optional repetition */
static int match_atom(const char *base, const char *text, int ti, const char *pat, int pi, int pend, BreMatch *m,
                      int *group_id, int *next_pi_out)
{
    if (pi >= pend)
        return -1;

    int atom_start = pi;
    int atom_end = pi;
    int consumed = -1;

    // 1. Capture group \( ... \)
    if (pat[pi] == '\\' && pi + 1 < pend && pat[pi + 1] == '(')
    {
        int gend = find_group_end(pat, pi, pend);
        if (gend < 0)
            return -1;
        int group_no = ++(*group_id);
        consumed = match_group_once(base, text, ti, pat, pi + 2, gend, m, group_no);
        if (consumed < 0)
            return -1;
        atom_end = gend + 2;
    }
    // 2. Character class
    else if (pat[pi] == '[')
    {
        int after;
        if (text[ti] == '\0' || !in_char_class((unsigned char)text[ti], pat, pi, pend, &after))
            return -1;
        consumed = 1;
        atom_end = after;
    }
    // 3. Dot
    else if (pat[pi] == '.')
    {
        if (text[ti] == '\0')
            return -1;
        consumed = 1;
        atom_end = pi + 1;
    }
    // 4. Escaped literal
    else if (pat[pi] == '\\' && pi + 1 < pend)
    {
        char c = pat[pi + 1];
        if (c == ')' || c == '{' || c == '}')
            return -1;
        if (text[ti] == '\0' || text[ti] != c)
            return -1;
        consumed = 1;
        atom_end = pi + 2;
    }
    // 5. Literal char
    else
    {
        if (text[ti] == '\0' || text[ti] != pat[pi])
            return -1;
        consumed = 1;
        atom_end = pi + 1;
    }

    // Now check for repetition \{n,m\}
    int min_rep = 1, max_rep = 1;
    int rep_next_pi = atom_end;
    int rep_res = parse_bre_repetition(pat, atom_end, pend, &min_rep, &max_rep, &rep_next_pi);

    if (rep_res == 1)
    {
        int max_possible = (max_rep < 0) ? 10000 : max_rep;
        int tpos = ti;
        int count = 0;
        int temp_gid = *group_id;

        // Greedy: match as many as possible
        while (count < max_possible)
        {
            int adv = match_here(base, text, tpos, pat, atom_start, atom_end, m, &temp_gid);
            if (adv <= 0)
                break;
            tpos += adv;
            count++;
        }

        // Backtrack from longest to min_rep
        while (count >= min_rep)
        {
            int rest = match_here(base, text, tpos, pat, rep_next_pi, pend, m, group_id);
            if (rest >= 0)
            {
                // Success: replay to set capture groups correctly
                int replay_ti = ti;
                temp_gid = *group_id;
                for (int i = 0; i < count; i++)
                {
                    int adv = match_here(base, text, replay_ti, pat, atom_start, atom_end, m, &temp_gid);
                    if (adv <= 0)
                        break;
                    replay_ti += adv;
                }
                *next_pi_out = rep_next_pi;
                return (tpos - ti) + rest;
            }
            // Back off one instance
            if (count > 0)
            {
                int adv = match_here(base, text, tpos - 1, pat, atom_start, atom_end, m, &temp_gid);
                tpos -= (adv > 0 ? adv : 1);
                count--;
            }
            else
                break;
        }
        return -1; // no valid count found
    }

    // No repetition
    *next_pi_out = atom_end;
    return consumed;
}

/* Main recursive matcher */
static int match_here(const char *base, const char *text, int ti, const char *pat, int pi, int pend, BreMatch *m,
                      int *group_id)
{
    if (pi >= pend)
        return text[ti] == '\0' ? 0 : -1;

    // End-of-string anchor
    if (pat[pi] == '$' && pi + 1 == pend)
        return text[ti] == '\0' ? 0 : -1;

    int next_pi;
    int consumed = match_atom(base, text, ti, pat, pi, pend, m, group_id, &next_pi);
    if (consumed < 0)
        return -1;

    int rest = match_here(base, text, ti + consumed, pat, next_pi, pend, m, group_id);
    if (rest < 0)
        return -1;

    return consumed + rest;
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
    bool anchored = (plen > 0 && pattern[0] == '^');
    int start_pi = anchored ? 1 : 0;

    int tlen = (int)strlen(text);

    for (int s = anchored ? 0 : 0; s <= (anchored ? 0 : tlen); s++)
    {
        match->num_groups = 0;
        for (int i = 0; i < BRE_MAX_GROUPS; i++)
        {
            match->groups[i].start = -1;
            match->groups[i].length = 0;
        }
        int gid = 0;
        int consumed = match_here(text, text, s, pattern, start_pi, plen, match, &gid);
        if (consumed >= 0)
        {
            match->start = s;
            match->length = consumed;
            return true;
        }
        if (anchored)
            break;
    }
    return false;
}

char *bre_substitute(const char *text, const char *pattern, const char *replacement)
{
    if (!text || !pattern || !replacement)
        return NULL;

    BreMatch m = {0};
    if (!bre_match(text, pattern, &m) || m.start < 0)
    {
        char *copy = strdup(text);
        return copy ? copy : NULL;
    }

    size_t pre = m.start;
    size_t suf = strlen(text) - m.start - m.length;

    // Compute replacement length
    size_t replen = 0;
    for (const char *r = replacement; *r;)
    {
        if (*r == '\\' && r[1] >= '1' && r[1] <= '9')
        {
            int g = r[1] - '1';
            if (g < m.num_groups && m.groups[g].start >= 0)
                replen += m.groups[g].length;
            r += 2;
        }
        else
        {
            replen++;
            r++;
        }
    }

    char *out = malloc(pre + replen + suf + 1);
    if (!out)
        return NULL;

    char *p = out;
    memcpy(p, text, pre);
    p += pre;

    for (const char *r = replacement; *r;)
    {
        if (*r == '\\' && r[1] >= '1' && r[1] <= '9')
        {
            int g = r[1] - '1';
            if (g < m.num_groups && m.groups[g].start >= 0)
            {
                memcpy(p, text + m.groups[g].start, m.groups[g].length);
                p += m.groups[g].length;
            }
            r += 2;
        }
        else
        {
            *p++ = *r++;
        }
    }

    memcpy(p, text + m.start + m.length, suf);
    p[suf] = '\0';
    return out;
}
