#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "bre.h"

// Strict-POSIX-oriented sed (no GNU/BSD extensions)
// Supported commands: p d q n = s y w r a i c N D P h H g G x l
// Not implemented: b, t, : (labels), { } groupings
// Addresses: line number, $, /re/, and ranges addr1,addr2
// Options: -n, -e script, -f scriptfile

#define MAX_CMDS 512
#define MAX_FILES 128
#define MAX_SCRIPT_LEN 65536
#define MAX_LINE 65536

typedef enum { ADDR_NONE, ADDR_LINE, ADDR_LAST, ADDR_REGEX } AddrType;

typedef struct {
    AddrType type;
    long line;        // for ADDR_LINE
    char *regex;      // for ADDR_REGEX
} Address;

typedef enum {
    CMD_P, CMD_D, CMD_Q, CMD_N, CMD_EQ,
    CMD_S, CMD_Y, CMD_W, CMD_R,
    CMD_A, CMD_I, CMD_C,
    CMD_NCAP, CMD_DCAP, CMD_PCAP,
    CMD_H, CMD_HAPP, CMD_G, CMD_GAPP, CMD_X,
    CMD_L
} CmdType;

typedef struct WriteFile {
    char *name;
    FILE *fp;
    bool truncate_done;
} WriteFile;

typedef struct SedCmd {
    Address a1;           // optional first address
    Address a2;           // optional second address (range)
    bool has_a1;
    bool has_a2;
    bool in_range;        // stateful for ranges

    CmdType type;

    // s command
    char *s_pat;          // pattern
    char *s_repl;         // replacement
    int s_occurrence;     // 0 = first (default), >0 specific occurrence, -1 = g (all)
    bool s_print;         // p flag
    char *s_wfile;        // w file flag

    // y command
    char *y_src;
    char *y_dst;

    // w command (standalone)
    char *w_file;

    // r command
    char *r_file;

    // a/i/c text
    char *text;           // includes embedded newlines as written
} SedCmd;

static SedCmd g_cmds[MAX_CMDS];
static int g_ncmds = 0;

static WriteFile g_wfiles[MAX_FILES];
static int g_nwfiles = 0;

static bool g_auto_print = true;
static bool is_last_line_in_file = false;
static FILE *g_out = NULL; /* non-POSIX: redirected output (defaults to stdout) */

static void free_address(Address *a) {
    if (a->type == ADDR_REGEX && a->regex) { free(a->regex); a->regex = NULL; }
}

static void free_cmd(SedCmd *c) {
    free_address(&c->a1);
    free_address(&c->a2);
    if (c->s_pat) free(c->s_pat);
    if (c->s_repl) free(c->s_repl);
    if (c->s_wfile) free(c->s_wfile);
    if (c->y_src) free(c->y_src);
    if (c->y_dst) free(c->y_dst);
    if (c->w_file) free(c->w_file);
    if (c->r_file) free(c->r_file);
    if (c->text) free(c->text);
}

static void cleanup_all(void) {
    for (int i = 0; i < g_ncmds; i++) free_cmd(&g_cmds[i]);
    for (int i = 0; i < g_nwfiles; i++) {
        if (g_wfiles[i].fp) fclose(g_wfiles[i].fp);
        if (g_wfiles[i].name) free(g_wfiles[i].name);
    }
}

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static char *substr_dup(const char *s, size_t len) {
    char *p = (char*)malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

// Trim trailing newline from a buffer (if present)
static void chomp(char *s) {
    size_t n = strlen(s);
    if (n && s[n-1] == '\n') s[n-1] = '\0';
}

// Read entire file to string (for -f scripts)
static char *read_file_to_string(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    char *buf = NULL; size_t cap = 0, len = 0;
    // Plain text read; treat input as UTF-8. If an UTF-8 BOM is present, skip it.
    unsigned char bom[3]; size_t b = fread(bom, 1, 3, fp);
    size_t start = 0;
    if (b == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        start = 3;
    } else {
        if (b > 0) fseek(fp, 0, SEEK_SET);
    }
    char chunk[4096]; size_t nr;
    while ((nr = fread(chunk, 1, sizeof chunk, fp)) > 0) {
        if (len + nr + 1 > cap) { size_t ncap = cap ? cap * 2 : 4096; while (ncap < len + nr + 1) ncap *= 2; char *nb = (char*)realloc(buf, ncap); if (!nb) { free(buf); fclose(fp); return NULL; } buf = nb; cap = ncap; }
        memcpy(buf + len, chunk, nr); len += nr; buf[len] = '\0';
    }
    fclose(fp);
    if (!buf) buf = xstrdup(""); else buf[len] = '\0';
    return buf;
}

// --- Address matching ---
static bool addr_matches(Address *a, const char *ps, long lineno) {
    switch (a->type) {
        case ADDR_NONE: return true; // no restriction
        case ADDR_LINE: return a->line == lineno;
        case ADDR_LAST: return is_last_line_in_file;
        case ADDR_REGEX: {
            BreMatch m;
            return bre_match(ps, a->regex, &m);
        }
        default: return false;
    }
}

// For ranges with '$', we signal end-of-range on last line.
// We'll pass a flag is_last_line to the executor.
static bool addr_is_last(Address *a) { return a->type == ADDR_LAST; }

// --- Write-file management ---
static FILE *get_wfile(const char *name, bool truncate_now) {
    for (int i = 0; i < g_nwfiles; i++) {
        if (strcmp(g_wfiles[i].name, name) == 0) {
            if (truncate_now && !g_wfiles[i].truncate_done) {
                // Reopen in write mode once to truncate
                if (g_wfiles[i].fp) fclose(g_wfiles[i].fp);
                g_wfiles[i].fp = fopen(name, "w");
                if (!g_wfiles[i].fp) return NULL;
                g_wfiles[i].truncate_done = true;
            }
            if (!g_wfiles[i].fp) {
                g_wfiles[i].fp = fopen(name, g_wfiles[i].truncate_done ? "a" : "w");
                if (!g_wfiles[i].fp) return NULL;
                g_wfiles[i].truncate_done = true;
            }
            return g_wfiles[i].fp;
        }
    }
    if (g_nwfiles >= MAX_FILES) return NULL;
    g_wfiles[g_nwfiles].name = xstrdup(name);
    g_wfiles[g_nwfiles].fp = fopen(name, truncate_now ? "w" : "a");
    if (!g_wfiles[g_nwfiles].fp) {
        free(g_wfiles[g_nwfiles].name);
        return NULL;
    }
    g_wfiles[g_nwfiles].truncate_done = true;
    g_nwfiles++;
    return g_wfiles[g_nwfiles - 1].fp;
}

// --- Substitute helper supporting g / Nth occurrence / p / w ---
static char *do_substitute(const char *text, const char *pattern, const char *replacement, int occurrence, bool *did_any, int *num_subs) {
    // occurrence: -1 = global, 0 = first only, >0 = that specific occurrence
    size_t tlen = strlen(text);
    size_t cap = tlen + 1;
    char *out = (char*)malloc(cap);
    if (!out) return NULL;
    size_t olen = 0;
    size_t pos = 0;
    int match_count = 0;
    *did_any = false;
    if (num_subs) *num_subs = 0;

    while (pos <= tlen) {
        BreMatch m;
        if (!bre_match(text + pos, pattern, &m) || m.start < 0) {
            // copy rest
            size_t rest = tlen - pos;
            if (olen + rest + 1 > cap) { cap = (olen + rest + 1) * 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
            memcpy(out + olen, text + pos, rest);
            olen += rest;
            break;
        }
        size_t mstart = pos + (size_t)m.start;
        size_t mlen = (size_t)m.length;
        // copy prefix
        size_t prefix = mstart - pos;
        if (olen + prefix + 1 > cap) { cap = (olen + prefix + 1) * 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
        memcpy(out + olen, text + pos, prefix);
        olen += prefix;

        match_count++;
        bool replace_this = false;
        if (occurrence == -1) replace_this = true;             // global
        else if (occurrence == 0 && match_count == 1) replace_this = true; // first only
        else if (occurrence > 0 && match_count == occurrence) replace_this = true;

        if (replace_this) {
            // expand replacement with \1..\9 from m relative to whole text
            // m.groups hold absolute positions relative to text+pos; fix up
            if (olen + 1 > cap) { cap *= 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
            for (const char *r = replacement; *r; r++) {
                if (*r == '\\' && r[1] >= '1' && r[1] <= '9') {
                    int g = r[1] - '1';
                    if (g >= 0 && g < m.num_groups && m.groups[g].start >= 0) {
                        size_t gs = (size_t)m.groups[g].start; // relative to (text+pos)
                        size_t gl = (size_t)m.groups[g].length;
                        // Convert to absolute in original text
                        if (olen + gl + 1 > cap) { cap = (olen + gl + 1) * 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
                        memcpy(out + olen, (text + pos) + gs, gl);
                        olen += gl;
                    }
                    r++;
                } else {
                    if (olen + 1 > cap) { cap *= 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
                    out[olen++] = *r;
                }
            }
            *did_any = true;
            if (num_subs) (*num_subs)++;
        } else {
            // keep original match
            if (olen + mlen + 1 > cap) { cap = (olen + mlen + 1) * 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
            memcpy(out + olen, text + mstart, mlen);
            olen += mlen;
        }

        // advance position
        pos = mstart + (mlen > 0 ? mlen : 1);

        if (occurrence > 0 && match_count >= occurrence) {
            // copy rest and finish
            size_t rest = tlen - pos;
            if (olen + rest + 1 > cap) { cap = (olen + rest + 1) * 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
            memcpy(out + olen, text + pos, rest);
            olen += rest;
            break;
        }

        if (occurrence == 0) {
            // first only: copy rest and finish
            size_t rest = tlen - pos;
            if (olen + rest + 1 > cap) { cap = (olen + rest + 1) * 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
            memcpy(out + olen, text + pos, rest);
            olen += rest;
            break;
        }
    }

    out[olen] = '\0';
    return out;
}

// --- Parser ---

typedef struct {
    const char *s; // script buffer
    size_t i;
    size_t n;
    int line_no;
} Parser;

static void ps_init(Parser *p, const char *s) { p->s = s; p->i = 0; p->n = strlen(s); p->line_no = 1; }

static int ps_peek(Parser *p) { return (p->i < p->n) ? (unsigned char)p->s[p->i] : -1; }
static int ps_get(Parser *p) {
    if (p->i >= p->n) return -1;
    char c = p->s[p->i++];
    if (c == '\n') p->line_no++;
    return (unsigned char)c;
}
static void ps_skip_ws(Parser *p) {
    while (p->i < p->n) {
        char c = p->s[p->i];
        if (c == ' ' || c == '\t') p->i++;
        else break;
    }
}

static bool ps_expect(Parser *p, char ch) {
    int c = ps_get(p);
    return c == ch;
}

static char *parse_delimited(Parser *p, char delim, bool allow_esc) {
    size_t start = p->i;
    char *buf = NULL; size_t cap = 0, len = 0;
    while (p->i < p->n) {
        char c = p->s[p->i++];
        if (allow_esc && c == '\\' && p->i < p->n) {
            char next = p->s[p->i++];
            // store literally next
            if (len + 2 > cap) { size_t nc = cap ? cap * 2 : 64; while (nc < len + 2) nc *= 2; char *nb = (char*)realloc(buf, nc); if (!nb) { free(buf); return NULL; } buf = nb; cap = nc; }
            buf[len++] = next;
            continue;
        }
        if (c == delim) break;
        if (len + 2 > cap) { size_t nc = cap ? cap * 2 : 64; while (nc < len + 2) nc *= 2; char *nb = (char*)realloc(buf, nc); if (!nb) { free(buf); return NULL; } buf = nb; cap = nc; }
        buf[len++] = c;
    }
    if (!buf) return xstrdup("");
    buf[len] = '\0';
    return buf;
}

static bool parse_address(Parser *p, Address *out) {
    ps_skip_ws(p);
    int c = ps_peek(p);
    if (c < 0) return false;
    if (c == '$') { ps_get(p); out->type = ADDR_LAST; out->line = -1; out->regex = NULL; return true; }
    if (isdigit(c)) {
        long val = 0;
        while (isdigit(ps_peek(p))) { val = val * 10 + (ps_get(p) - '0'); }
        out->type = ADDR_LINE; out->line = val; out->regex = NULL; return true;
    }
    if (c == '/') {
        ps_get(p);
        char *re = parse_delimited(p, '/', true);
        if (!re) return false;
        out->type = ADDR_REGEX; out->regex = re; out->line = -1; return true;
    }
    return false;
}

static bool parse_text_block(Parser *p, char **text_out) {
    // Text for a/i/c: command line has optional backslash; then the following
    // script line(s) become the text. We collect until a newline not preceded
    // by backslash. Each continued line gets a literal '\n'.
    char *acc = NULL; size_t cap = 0, len = 0;
    // If the current char is '\\', consume it if followed by '\n'
    if (ps_peek(p) == '\\') {
        size_t save_i = p->i; int save_ln = p->line_no;
        ps_get(p);
        if (ps_peek(p) == '\n') { ps_get(p); }
        else { // treat as literal backslash
            p->i = save_i; p->line_no = save_ln;
        }
    } else if (ps_peek(p) == '\n') {
        ps_get(p);
    }
    for (;;) {
        // grab until end of line or EOF
        while (p->i < p->n) {
            char c = p->s[p->i++];
            if (c == '\n') { // end line
                // Check if previous char was backslash continuation
                if (len > 0 && acc[len-1] == '\\') {
                    len--; // remove the backslash
                    // continue with another script line
                    // Append a literal '\n' to the accumulated text
                    if (len + 1 + 1 > cap) { size_t nc = cap ? cap * 2 : 64; while (nc < len + 2) nc *= 2; char *nb = (char*)realloc(acc, nc); if (!nb) { free(acc); return false; } acc = nb; cap = nc; }
                    acc[len++] = '\n';
                    break; // move to next script line
                } else {
                    // terminate the text block here
                    if (!acc) acc = xstrdup("");
                    acc[len] = '\0';
                    *text_out = acc;
                    return true;
                }
            } else {
                if (len + 2 > cap) { size_t nc = cap ? cap * 2 : 64; while (nc < len + 2) nc *= 2; char *nb = (char*)realloc(acc, nc); if (!nb) { free(acc); return false; } acc = nb; cap = nc; }
                acc[len++] = c;
            }
        }
        if (p->i >= p->n) {
            if (!acc) acc = xstrdup("");
            acc[len] = '\0';
            *text_out = acc;
            return true;
        }
    }
}

static bool parse_one_command(Parser *p, SedCmd *cmd) {
    memset(cmd, 0, sizeof *cmd);

    ps_skip_ws(p);
    if (p->i >= p->n) return false;

    // addresses
    Address a1 = {0}, a2 = {0};
    size_t save_i = p->i; int save_ln = p->line_no;
    if (parse_address(p, &a1)) {
        ps_skip_ws(p);
        if (ps_peek(p) == ',') {
            ps_get(p);
            ps_skip_ws(p);
            if (!parse_address(p, &a2)) { free_address(&a1); return false; }
            cmd->has_a1 = true; cmd->a1 = a1;
            cmd->has_a2 = true; cmd->a2 = a2;
        } else {
            cmd->has_a1 = true; cmd->a1 = a1; cmd->has_a2 = false;
        }
    } else {
        p->i = save_i; p->line_no = save_ln;
    }

    ps_skip_ws(p);
    int c = ps_get(p);
    if (c < 0) return false;

    switch (c) {
        case 'p': cmd->type = CMD_P; break;
        case 'd': cmd->type = CMD_D; break;
        case 'q': cmd->type = CMD_Q; break;
        case 'n': cmd->type = CMD_N; break;
        case '=': cmd->type = CMD_EQ; break;
        case 'h': cmd->type = CMD_H; break;
        case 'H': cmd->type = CMD_HAPP; break;
        case 'g': cmd->type = CMD_G; break;
        case 'G': cmd->type = CMD_GAPP; break;
        case 'x': cmd->type = CMD_X; break;
        case 'N': cmd->type = CMD_NCAP; break;
        case 'D': cmd->type = CMD_DCAP; break;
        case 'P': cmd->type = CMD_PCAP; break;
        case 'l': cmd->type = CMD_L; break;
        case 'w': {
            cmd->type = CMD_W;
            ps_skip_ws(p);
            // filename to end of line or until ';'
            size_t start = p->i;
            while (p->i < p->n && p->s[p->i] != '\n' && p->s[p->i] != ';') p->i++;
            size_t len = p->i - start;
            while (len > 0 && isspace((unsigned char)p->s[start + len - 1])) len--;
            cmd->w_file = substr_dup(p->s + start, len);
            break;
        }
        case 'r': {
            cmd->type = CMD_R;
            ps_skip_ws(p);
            size_t start = p->i;
            while (p->i < p->n && p->s[p->i] != '\n' && p->s[p->i] != ';') p->i++;
            size_t len = p->i - start;
            while (len > 0 && isspace((unsigned char)p->s[start + len - 1])) len--;
            cmd->r_file = substr_dup(p->s + start, len);
            break;
        }
        case 'a': case 'i': case 'c': {
            cmd->type = (c == 'a') ? CMD_A : (c == 'i') ? CMD_I : CMD_C;
            // The rest of this command consumes the following text block
            if (!parse_text_block(p, &cmd->text)) return false;
            break;
        }
        case 'y': {
            cmd->type = CMD_Y;
            int delim = ps_get(p);
            if (delim <= 0 || delim == '\n') return false;
            cmd->y_src = parse_delimited(p, (char)delim, true);
            if (!cmd->y_src) return false;
            // Parse dst
            cmd->y_dst = parse_delimited(p, (char)delim, true);
            if (!cmd->y_dst) return false;
            // lengths must be equal
            if (strlen(cmd->y_src) != strlen(cmd->y_dst)) return false;
            break;
        }
        case 's': {
            cmd->type = CMD_S;
            int delim = ps_get(p);
            if (delim <= 0 || delim == '\n') return false;
            cmd->s_pat = parse_delimited(p, (char)delim, true);
            if (!cmd->s_pat) return false;
            cmd->s_repl = parse_delimited(p, (char)delim, true);
            if (!cmd->s_repl) return false;
            // flags
            cmd->s_occurrence = 0; // first only by default
            cmd->s_print = false;
            cmd->s_wfile = NULL;
            ps_skip_ws(p);
            while (p->i < p->n) {
                int f = ps_peek(p);
                if (f == 'g') { cmd->s_occurrence = -1; ps_get(p); }
                else if (f == 'p') { cmd->s_print = true; ps_get(p); }
                else if (isdigit(f)) {
                    int num = 0;
                    while (isdigit(ps_peek(p))) { num = num * 10 + (ps_get(p) - '0'); }
                    cmd->s_occurrence = num;
                } else if (f == 'w') {
                    ps_get(p);
                    ps_skip_ws(p);
                    size_t start = p->i;
                    while (p->i < p->n && p->s[p->i] != '\n' && p->s[p->i] != ';') p->i++;
                    size_t len = p->i - start;
                    while (len > 0 && isspace((unsigned char)p->s[start + len - 1])) len--;
                    cmd->s_wfile = substr_dup(p->s + start, len);
                } else {
                    break;
                }
            }
            break;
        }
        default:
            return false;
    }

    // eat optional separators ';' and whitespace, and optional newline
    ps_skip_ws(p);
    if (ps_peek(p) == ';') { ps_get(p); }
    return true;
}

static bool parse_script(const char *script) {
    Parser p; ps_init(&p, script);
    while (p.i < p.n) {
        ps_skip_ws(&p);
        if (p.i >= p.n) break;
        if (p.s[p.i] == '\r') { p.i++; continue; }
        if (p.s[p.i] == '\n') { p.i++; p.line_no++; continue; }
        if (g_ncmds >= MAX_CMDS) { fprintf(stderr, "sed: too many commands\n"); return false; }
        SedCmd *cmd = &g_cmds[g_ncmds];
        if (!parse_one_command(&p, cmd)) {
            fprintf(stderr, "sed: parse error near script line %d\n", p.line_no);
            return false;
        }
        g_ncmds++;
        // move to end of line or after ';'
        while (p.i < p.n && p.s[p.i] != '\n') p.i++;
        if (p.i < p.n && p.s[p.i] == '\n') { p.i++; p.line_no++; }
    }
    return true;
}

// --- Execution ---

typedef struct {
    char *ps;      // pattern space (includes any embedded newlines as read)
    char *hs;      // hold space
    long lineno;   // current input line number (1-based)
    bool quit_now; // set by 'q' to stop processing after current cycle printing
} ExecState;

static void free_exec(ExecState *st) {
    if (st->ps) free(st->ps);
    if (st->hs) free(st->hs);
}

static bool apply_addresses(SedCmd *c, const char *ps, long lineno) {
    if (!c->has_a1) return true;
    bool match1 = addr_matches(&c->a1, ps, lineno);
    if (!c->has_a2) return match1;
    // range logic
    if (!c->in_range) {
        if (match1) {
            c->in_range = true;
            // single line unless a2 also matches immediately when it is same line with /re/ or number 0? POSIX: when addr1 matches, the range is active including current line; if a2 is '$' and this is not last line, remains until last; We'll mark end when a2 matches.
        }
    }
    if (c->in_range) {
        bool end_now = false;
        if (addr_is_last(&c->a2)) {
            end_now = is_last_line_in_file;
        } else {
            end_now = addr_matches(&c->a2, ps, lineno);
        }
        if (end_now) {
            // Per POSIX, after executing on this line, range ends
            // We'll let caller execute, then reset here? We return true now and mark that range ends after.
            // Use a side signal by setting a2.type to ADDR_NONE? Better: store a flag in cmd; We'll reset after caller runs.
        }
        return true;
    }
    return false;
}

static void maybe_end_range_after_line(SedCmd *c, const char *ps, long lineno) {
    if (c->has_a1 && c->has_a2 && c->in_range) {
        bool end_now = false;
        if (addr_is_last(&c->a2)) end_now = is_last_line_in_file;
        else end_now = addr_matches(&c->a2, ps, lineno);
        if (end_now) c->in_range = false;
    }
}

static void cmd_print_line_number(long lineno) {
    fprintf(g_out ? g_out : stdout, "%ld\n", lineno);
}

static void cmd_print_l(const char *ps) {
    for (const unsigned char *p = (const unsigned char*)ps; *p; p++) {
        unsigned char c = *p;
        if (c == '\n') {
            fputs("\\n", g_out ? g_out : stdout);
        } else if (isprint(c) && c != '\\') {
            fputc(c, g_out ? g_out : stdout);
        } else {
            switch (c) {
                case '\\': fputs("\\\\", g_out ? g_out : stdout); break;
                case '\a': fputs("\\a", g_out ? g_out : stdout); break;
                case '\b': fputs("\\b", g_out ? g_out : stdout); break;
                case '\t': fputs("\\t", g_out ? g_out : stdout); break;
                case '\r': fputs("\\r", g_out ? g_out : stdout); break;
                case '\f': fputs("\\f", g_out ? g_out : stdout); break;
                case '\v': fputs("\\v", g_out ? g_out : stdout); break;
                default: {
                    char buf[5];
                    snprintf(buf, sizeof buf, "\\%03o", (unsigned)c);
                    fputs(buf, g_out ? g_out : stdout);
                }
            }
        }
    }
    fputs("$\n", g_out ? g_out : stdout);
}

static bool exec_cmds_on_line(ExecState *st) {
    bool printed_now = false;
    for (int i = 0; i < g_ncmds; i++) {
        SedCmd *c = &g_cmds[i];
        if (!apply_addresses(c, st->ps, st->lineno)) continue;

        switch (c->type) {
            case CMD_P: fputs(st->ps, g_out ? g_out : stdout); printed_now = true; break;
            case CMD_D: return false; // delete pattern space, start next cycle
            case CMD_Q: st->quit_now = true; return true;
            case CMD_EQ: cmd_print_line_number(st->lineno); break;
            case CMD_N: {
                // 'n' handled in driver: here we signal by special marker
                // We use a sentinel by setting st->ps to NULL and return true to indicate print (if auto) handled by driver
                // But POSIX: if auto-print is on, print current pattern space, then read next line and start next cycle
                if (g_auto_print) fputs(st->ps, g_out ? g_out : stdout);
                // mark special by setting st->ps to empty to indicate driver should fetch next line immediately
                free(st->ps); st->ps = NULL; return true;
            }
            case CMD_W: {
                FILE *fp = get_wfile(c->w_file, true);
                if (fp) fputs(st->ps, fp);
                break;
            }
            case CMD_R: {
                // Append file content to output after current pattern space is printed (POSIX 'r' appends after current line is printed)
                // Implementation: we will print file content immediately, but since sed prints default at end, this matches behavior of appending after current line
                FILE *fp = fopen(c->r_file, "r");
                if (fp) {
                    char buf[4096]; size_t n;
                    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) fwrite(buf, 1, n, g_out ? g_out : stdout);
                    fclose(fp);
                }
                break;
            }
            case CMD_A: {
                // Append text to output after current line is printed; emulate by printing now
                fputs(c->text, g_out ? g_out : stdout);
                fputc('\n', g_out ? g_out : stdout);
                break;
            }
            case CMD_I: {
                // Insert text before current line output; emulate by printing now
                fputs(c->text, g_out ? g_out : stdout);
                fputc('\n', g_out ? g_out : stdout);
                break;
            }
            case CMD_C: {
                // Replace pattern space by text; if address is a range, only once
                free(st->ps);
                size_t L = strlen(c->text);
                st->ps = (char*)malloc(L + 2);
                if (!st->ps) st->ps = xstrdup("");
                memcpy(st->ps, c->text, L);
                st->ps[L] = '\n'; st->ps[L+1] = '\0';
                break;
            }
            case CMD_S: {
                bool did_any = false; int subs = 0;
                char *res = do_substitute(st->ps, c->s_pat, c->s_repl, c->s_occurrence, &did_any, &subs);
                if (res) { free(st->ps); st->ps = res; }
                if (did_any && c->s_print) { fputs(st->ps, g_out ? g_out : stdout); printed_now = true; }
                if (did_any && c->s_wfile) {
                    FILE *fp = get_wfile(c->s_wfile, true);
                    if (fp) fputs(st->ps, fp);
                }
                break;
            }
            case CMD_Y: {
                size_t n = strlen(st->ps);
                for (size_t k = 0; k < n; k++) {
                    unsigned char ch = (unsigned char)st->ps[k];
                    char *pos = strchr(c->y_src, ch);
                    if (pos) {
                        size_t idx = (size_t)(pos - c->y_src);
                        st->ps[k] = c->y_dst[idx];
                    }
                }
                break;
            }
            case CMD_NCAP: {
                // Append next input line to pattern space with newline. Driver handles fetching next line into tmp.
                // We'll mark with a special token: an embedded 0x01 char at end to signal N needed.
                size_t L = strlen(st->ps);
                st->ps = (char*)realloc(st->ps, L + 2);
                if (!st->ps) return false;
                st->ps[L] = 1; st->ps[L+1] = '\0';
                return true;
            }
            case CMD_DCAP: {
                // Delete up to first newline; if none, act like d
                char *nl = strchr(st->ps, '\n');
                if (!nl) return false; // behave like d
                size_t rest = strlen(nl + 1);
                memmove(st->ps, nl + 1, rest + 1);
                // restart script from first command: emulate by restarting loop
                i = -1; // will ++ to 0
                break;
            }
            case CMD_PCAP: {
                // Print up to first newline
                char *nl = strchr(st->ps, '\n');
                if (nl) {
                    size_t cnt = (size_t)(nl - st->ps + 1);
                    fwrite(st->ps, 1, cnt, g_out ? g_out : stdout);
                } else {
                    fputs(st->ps, g_out ? g_out : stdout);
                }
                printed_now = true;
                break;
            }
            case CMD_H: {
                free(st->hs);
                st->hs = xstrdup(st->ps);
                break;
            }
            case CMD_HAPP: {
                size_t hlen = st->hs ? strlen(st->hs) : 0;
                size_t plen = strlen(st->ps);
                char *nb = (char*)realloc(st->hs, hlen + plen + 1);
                if (!nb) break;
                memcpy(nb + hlen, st->ps, plen + 1);
                st->hs = nb;
                break;
            }
            case CMD_G: {
                free(st->ps);
                st->ps = st->hs ? xstrdup(st->hs) : xstrdup("");
                break;
            }
            case CMD_GAPP: {
                size_t plen = strlen(st->ps);
                size_t hlen = st->hs ? strlen(st->hs) : 0;
                char *nb = (char*)realloc(st->ps, plen + hlen + 1);
                if (!nb) break;
                memcpy(nb + plen, st->hs ? st->hs : "", hlen);
                nb[plen + hlen] = '\0';
                st->ps = nb;
                break;
            }
            case CMD_X: {
                char *tmp = st->ps; st->ps = st->hs; st->hs = tmp;
                if (!st->ps) st->ps = xstrdup("");
                break;
            }
            case CMD_L: {
                cmd_print_l(st->ps);
                printed_now = true;
                break;
            }
        }

        // handle end of range
        maybe_end_range_after_line(c, st->ps, st->lineno);

        // N special marker: driver will perform actual read/append; we just stop executing further commands for this cycle
        if (c->type == CMD_NCAP) return true;
    }
    (void)printed_now; // not used beyond this point; default printing handled by driver
    return true;
}

static bool process_files(char **files, int nfiles) {
    ExecState st = {0};
    st.hs = NULL; st.ps = NULL; st.lineno = 0; st.quit_now = false;

    char line[MAX_LINE];
    for (int fi = 0; fi < nfiles || (nfiles == 0 && fi == 0); fi++) {
        const char *name = (nfiles == 0) ? "-" : files[fi];
        FILE *fp = NULL;
        if (strcmp(name, "-") == 0) fp = stdin; else fp = fopen(name, "r");
        if (!fp) { fprintf(stderr, "sed: cannot open %s\n", name); free_exec(&st); return false; }

        bool eof = false;
        while (!eof) {
            if (!fgets(line, sizeof line, fp)) { eof = true; break; }
            st.lineno++;
            free(st.ps);
            st.ps = xstrdup(line);

            // Determine if this is the last line in file by peeking
            long pos_before = ftell(fp);
            int nextc = fgetc(fp);
            if (nextc == EOF) is_last_line_in_file = true; else { is_last_line_in_file = false; ungetc(nextc, fp); }

            bool cont = exec_cmds_on_line(&st);
            if (!cont) {
                // 'd' occurred; start next cycle; no default print
                continue;
            }

            // Handle special 'n' signaling via st.ps == NULL
            if (st.ps == NULL) {
                // read next line into pattern space; restart cycle
                if (!fgets(line, sizeof line, fp)) { eof = true; break; }
                st.lineno++;
                st.ps = xstrdup(line);
                if (st.quit_now) { if (g_auto_print && st.ps) fputs(st.ps, g_out ? g_out : stdout); free_exec(&st); if (fp != stdin) fclose(fp); return true; }
                // restart cycle from first command
                // default printing already performed for previous pattern space
                // Continue to next iteration which will exec commands on this new line
                // fall-through to printing logic at bottom is skipped intentionally
                // We'll just loop back
                // But we need to ensure we don't auto-print again; continue to top
                // rely on continue
                // Mark last-line flag again
                long pos_b = ftell(fp);
                int c2 = fgetc(fp);
                if (c2 == EOF) is_last_line_in_file = true; else { is_last_line_in_file = false; ungetc(c2, fp); }

                cont = exec_cmds_on_line(&st);
                if (!cont) continue;
            }

            // Handle 'N' marker (0x01 at end)
            if (st.ps && st.ps[0] && st.ps[strlen(st.ps) ? strlen(st.ps) - 1 : 0] == 1) {
                // remove marker
                st.ps[strlen(st.ps) - 1] = '\0';
                // append next line (including newline) if available
                if (fgets(line, sizeof line, fp)) {
                    st.lineno++;
                    size_t L1 = strlen(st.ps), L2 = strlen(line);
                    char *nb = (char*)realloc(st.ps, L1 + 1 + L2 + 1);
                    if (!nb) { free_exec(&st); if (fp != stdin) fclose(fp); return false; }
                    st.ps = nb;
                    st.ps[L1] = '\n';
                    memcpy(st.ps + L1 + 1, line, L2 + 1);
                    // restart command execution from next command after N per POSIX, but we approximated by re-running from start which is acceptable for many scripts
                }
            }

            if (st.quit_now) {
                if (g_auto_print && st.ps) fputs(st.ps, g_out ? g_out : stdout);
                free_exec(&st);
                if (fp != stdin) fclose(fp);
                return true;
            }

            if (g_auto_print && st.ps) fputs(st.ps, g_out ? g_out : stdout);
        }
        if (fp != stdin) fclose(fp);
    }

    free_exec(&st);
    return true;
}

static void print_usage(void) {
    printf("Usage: sed [-n] [-o outfile] [-e script]... [-f scriptfile]... [scriptfile] [infile] [outfile]\n");
    printf("Non-POSIX additions: -o outfile for redirect; positional script file + input + output when no -e/-f used.\n");
}

int main(int argc, char *argv[]) {
    atexit(cleanup_all);

    char *script_acc = NULL; size_t acc_cap = 0, acc_len = 0;
    char *inline_script = NULL; // if provided inline (fallback when file not readable)
    const char *pos_script_file = NULL; /* positional script file */
    const char *pos_in_file = NULL;     /* positional input file  */
    const char *pos_out_file = NULL;    /* positional output file */

    // Parse options
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) { g_auto_print = false; continue; }
        else if (strcmp(argv[i], "-e") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "sed: -e requires argument\n"); return 1; }
            const char *s = argv[++i];
            size_t L = strlen(s);
            if (acc_len + L + 2 > acc_cap) { size_t nc = acc_cap ? acc_cap * 2 : 1024; while (nc < acc_len + L + 2) nc *= 2; char *nb = (char*)realloc(script_acc, nc); if (!nb) { free(script_acc); return 1; } script_acc = nb; acc_cap = nc; }
            memcpy(script_acc + acc_len, s, L); acc_len += L; script_acc[acc_len++] = '\n'; script_acc[acc_len] = '\0';
            continue;
        } else if (strcmp(argv[i], "-f") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "sed: -f requires path\n"); return 1; }
            char *file_script = read_file_to_string(argv[++i]);
            if (!file_script) { fprintf(stderr, "sed: cannot read %s\n", argv[i]); return 1; }
            size_t L = strlen(file_script);
            if (acc_len + L + 1 > acc_cap) { size_t nc = acc_cap ? acc_cap * 2 : 1024; while (nc < acc_len + L + 1) nc *= 2; char *nb = (char*)realloc(script_acc, nc); if (!nb) { free(script_acc); free(file_script); return 1; } script_acc = nb; acc_cap = nc; }
            memcpy(script_acc + acc_len, file_script, L); acc_len += L; script_acc[acc_len] = '\0';
            free(file_script);
            continue;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "sed: -o requires path\n"); return 1; }
            pos_out_file = argv[++i];
            continue;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            print_usage();
            return 1;
        }
        break; // first non-option
    }

    if (!script_acc) {
        if (i >= argc) { print_usage(); return 1; }
        const char *candidate = argv[i++];
        char *file_script = read_file_to_string(candidate);
        if (file_script) {
            pos_script_file = candidate;
            size_t L = strlen(file_script);
            script_acc = (char*)malloc(L + 1);
            if (!script_acc) { free(file_script); return 1; }
            memcpy(script_acc, file_script, L + 1);
            /* Ensure terminal newline for simpler parsing */
            if (L == 0 || script_acc[L-1] != '\n') {
                char *tmp = (char*)realloc(script_acc, L + 2);
                if (!tmp) { free(file_script); free(script_acc); return 1; }
                script_acc = tmp; script_acc[L] = '\n'; script_acc[L+1] = '\0';
            }
            free(file_script);
            /* Optional input file */
            if (i < argc) pos_in_file = argv[i++];
            /* Optional output file if not via -o */
            if (i < argc && !pos_out_file) pos_out_file = argv[i++];
        } else {
            /* Treat candidate as inline script text */
            inline_script = candidate;
            size_t L = strlen(inline_script);
            script_acc = (char*)malloc(L + 2);
            if (!script_acc) return 1;
            memcpy(script_acc, inline_script, L); script_acc[L] = '\n'; script_acc[L+1] = '\0';
        }
    }

    /* Normalize script: remove carriage returns to simplify parsing */
    if (script_acc) {
        size_t sl = strlen(script_acc); size_t w = 0;
        for (size_t r = 0; r < sl; r++) {
            char ch = script_acc[r];
            if (ch == '\r') continue; /* drop */
            script_acc[w++] = ch;
        }
        script_acc[w] = '\0';
        /* script normalized */
    }
    if (!parse_script(script_acc)) return 1;

    /* Open output file if requested */
    if (pos_out_file) {
        g_out = fopen(pos_out_file, "w");
        if (!g_out) { fprintf(stderr, "sed: cannot open output %s\n", pos_out_file); return 1; }
    }
    char *files[MAX_FILES]; int nfiles = 0;
    if (pos_in_file) {
        files[nfiles++] = (char*)pos_in_file;
    } else {
        for (; i < argc && nfiles < MAX_FILES; i++) files[nfiles++] = argv[i];
    }
    /* Inline script positional output file heuristic: if inline script used (inline_script != NULL), no -o given, and more than one file arg, treat last as output */
    if (!pos_out_file && inline_script && nfiles > 1) {
        pos_out_file = files[nfiles - 1];
        nfiles--; /* remove from input list */
        g_out = fopen(pos_out_file, "w");
        if (!g_out) { fprintf(stderr, "sed: cannot open output %s\n", pos_out_file); return 1; }
    }

    bool ok = process_files(files, nfiles);
    if (g_out && g_out != stdout) fclose(g_out);
    return ok ? 0 : 1;
}
