#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ed.h"
// BRE regex engine for s/// support
#include "bre.h"

// Input source (stdin or script file)
static FILE *input_fp = NULL;

#ifdef LED_TEST
#define PRINTF(...)							\
    do									\
    {									\
        fputs("# ", stdout);						\
        printf(__VA_ARGS__);						\
    } while (0)
#else
#define PRINTF(...) printf(__VA_ARGS__)
#endif

static char *my_strdup(const char *s);
void init_editor(Editor *ed);
int parse_address(Editor *ed, const char *addr);
AddressRange parse_address_range(Editor *ed, const char *range_str);
void append_line(Editor *ed, int addr);
void insert_line(Editor *ed, int addr);
void print_line(Editor *ed, int addr);
void print_range(Editor *ed, AddressRange range);
void edit_file(Editor *ed, const char *filename);
void forced_edit_file(Editor *ed, const char *filename);
void read_file_at_address(Editor *ed, int addr, const char *filename);
void write_append_file(Editor *ed, AddressRange range, const char *filename);
void change_range(Editor *ed, AddressRange range);
void move_range(Editor *ed, AddressRange range, int dest_addr);
void copy_range(Editor *ed, AddressRange range, int dest_addr);
void join_range(Editor *ed, AddressRange range);
void print_numbered_range(Editor *ed, AddressRange range);
void print_list_range(Editor *ed, AddressRange range);
void delete_line(Editor *ed, int addr);
void write_file(Editor *ed, const char *filename);
void free_editor(Editor *ed);
#ifndef LED_TEST
static void execute_command(Editor *ed, const char *cmd);
#endif
void load_file(Editor *ed, const char *filename);
static char *read_full_line(FILE *fp, int *had_newline);
static void critical_error(Editor *ed); // forward declaration for set_error
static void set_error(Editor *ed, const char *msg);
static void update_marks_after_delete(Editor *ed, int start_line, int num_deleted);
static void update_marks_after_insert(Editor *ed, int insert_line, int num_inserted);

// Undo support
static void prepare_undo(Editor *ed)
{
    if (ed->undo_lines)
    {
        for (int i = 0; i < ed->undo_num_lines; i++)
            free(ed->undo_lines[i]);
        free(ed->undo_lines);
        ed->undo_lines = NULL;
    }
    ed->undo_num_lines = 0;
    ed->undo_current_line = ed->current_line;
    // Only allocate/copy when there are lines to snapshot
    if (ed->num_lines > 0)
    {
        ed->undo_lines = (char **)malloc(ed->num_lines * sizeof(char *));
        if (!ed->undo_lines)
            critical_error(ed);
        for (int i = 0; i < ed->num_lines; i++)
        {
            ed->undo_lines[i] = my_strdup(ed->lines[i]);
            if (!ed->undo_lines[i])
                critical_error(ed);
        }
        ed->undo_num_lines = ed->num_lines;
    }
    ed->undo_valid = 1;
}
// Exposed in header as well
void substitute_range(Editor *ed, AddressRange range, const char *pattern, const char *replacement, int global);
void set_verbose(Editor *ed, int on)
{
    ed->verbose = on ? 1 : 0;
}
const char *get_last_error(Editor *ed)
{
    return ed->last_error;
}
void clear_last_error(Editor *ed)
{
    if (ed->last_error)
    {
        free(ed->last_error);
        ed->last_error = NULL;
    }
}

static void set_error(Editor *ed, const char *msg)
{
    if (!msg)
        msg = "Unknown error";
    // Store last error (duplicate)
    if (ed->last_error)
        free(ed->last_error);
    ed->last_error = my_strdup(msg);
    if (!ed->last_error)
    {
        // Allocation failure while storing error: escalate
        critical_error(ed);
    }
    if (ed->verbose)
    {
        PRINTF("%s\n", msg);
    }
    else
    {
        PRINTF("?\n");
    }
}

// Update marks after deleting lines
// start_line: 0-based index of first deleted line
// num_deleted: number of lines deleted
static void update_marks_after_delete(Editor *ed, int start_line, int num_deleted)
{
    for (int i = 0; i < 26; i++)
    {
        if (ed->marks[i] < 0)
            continue; // Mark not set
        
        if (ed->marks[i] >= start_line && ed->marks[i] < start_line + num_deleted)
        {
            // Mark was in deleted range - invalidate it
            ed->marks[i] = -1;
        }
        else if (ed->marks[i] >= start_line + num_deleted)
        {
            // Mark was after deleted range - shift it down
            ed->marks[i] -= num_deleted;
        }
        // Marks before start_line are unaffected
    }
}

// Update marks after inserting lines
// insert_line: 0-based index where insertion starts
// num_inserted: number of lines inserted
static void update_marks_after_insert(Editor *ed, int insert_line, int num_inserted)
{
    for (int i = 0; i < 26; i++)
    {
        if (ed->marks[i] < 0)
            continue; // Mark not set
        
        if (ed->marks[i] >= insert_line)
        {
            // Mark was at or after insertion point - shift it up
            ed->marks[i] += num_inserted;
        }
        // Marks before insert_line are unaffected
    }
}

char *my_strdup(const char *s)
{
    if (s == NULL)
        return NULL;
    size_t len = strlen(s) + 1; // Include null terminator
    char *dup = (char *)malloc(len);
    if (dup == NULL)
        return NULL; // Allocation failed
    strcpy(dup, s);
    return dup;
}

// Search helper for regex addresses: returns 1-based line number or 0 if not found
static int search_pattern(Editor *ed, const char *pattern, size_t pat_len, bool forward)
{
    if (ed->num_lines == 0)
        return 0;

    // Create null-terminated pattern
    char pat[MAX_LINE];
    if (pat_len >= sizeof(pat))
        return 0;
    strncpy(pat, pattern, pat_len);
    pat[pat_len] = '\0';

    BreMatch m;
    int start = (ed->current_line >= 0) ? ed->current_line : 0;

    if (forward)
    {
        // Search forward from current+1, wrapping
        for (int idx = start + 1; idx < ed->num_lines; idx++)
        {
            if (bre_match(ed->lines[idx], pat, &m) == BRE_OK)
            {
                return idx + 1; // Return 1-based
            }
        }
        // Wrap to beginning
        for (int idx = 0; idx <= start; idx++)
        {
            if (bre_match(ed->lines[idx], pat, &m) == BRE_OK)
            {
                return idx + 1; // Return 1-based
            }
        }
    }
    else
    {
        // Search backward from current-1, wrapping
        for (int idx = start - 1; idx >= 0; idx--)
        {
            if (bre_match(ed->lines[idx], pat, &m) == BRE_OK)
            {
                return idx + 1; // Return 1-based
            }
        }
        // Wrap to end
        for (int idx = ed->num_lines - 1; idx >= start; idx--)
        {
            if (bre_match(ed->lines[idx], pat, &m) == BRE_OK)
            {
                return idx + 1; // Return 1-based
            }
        }
    }

    return 0; // Not found
}

/* Parse a single address component.
 * Returns:
 *   ADDR_NONE (-1) if no address found
 *   ADDR_ERROR (-2) on syntax error
 *   Otherwise returns 1-based line number
 */
int parse_one_address(const char **pp, int current, int last_line, const int marks[26])
{
    const char *p = *pp;

    // Empty → no address
    if (!*p || *p == ',' || *p == ';' || *p == '\n' || isspace((unsigned char)*p))
        return ADDR_NONE;

    int sign = 1;
    int base = 0;
    bool relative = false;

    // Optional + or -
    if (*p == '+')
    {
        p++;
        relative = true;
    }
    else if (*p == '-')
    {
        p++;
        sign = -1;
        relative = true;
    }

    // Optional digits after +/- (only parse if we saw +/-)
    int offset = 0;
    if (relative)
    {
        while (isdigit((unsigned char)*p))
        {
            int digit = (*p - '0');
            // Check for overflow before multiplication
            if (offset > INT_MAX / 10 || (offset == INT_MAX / 10 && digit > INT_MAX % 10))
                return ADDR_ERROR;
            offset = offset * 10 + digit;
            p++;
        }
        base = current; // relative to current line
    }

    // Now the main address forms
    // If we have a relative offset with no following address, just apply it
    if (relative && !*p)
    {
        goto apply_offset;
    }

    if (*p == '$')
    {
        base = last_line;
        p++;
    }
    else if (*p == '.')
    {
        base = current;
        p++;
    }
    else if (*p == '0' && !relative && !offset)
    {
        // Only "0" by itself (not "+0" or "00")
        const char *t = p + 1;
        while (isdigit((unsigned char)*t))
            t++;
        // After while loop above, *t is guaranteed to not be a digit
        // Check if followed by +/- (which would indicate this isn't standalone "0")
        if (*t != '+' && *t != '-')
        {
            base = 0;
            p++;
            goto apply_offset;
        }
    }
    else if (isdigit((unsigned char)*p))
    {
        base = 0;
        while (isdigit((unsigned char)*p))
        {
            int digit = (*p - '0');
            // Check for overflow before multiplication
            if (base > INT_MAX / 10 || (base == INT_MAX / 10 && digit > INT_MAX % 10))
                return ADDR_ERROR;
            base = base * 10 + digit;
            p++;
        }
    }
    else if (*p == '\'' && p[1] >= 'a' && p[1] <= 'z')
    {
        int mark_idx = marks[p[1] - 'a'];
        // Check if mark is set and points to a valid line
        if (mark_idx < 0 || mark_idx >= last_line)
            return ADDR_ERROR; // unmarked or out of bounds
        base = mark_idx + 1;   // Convert from 0-based to 1-based
        p += 2;
    }
    else if (*p == '/' || *p == '?')
    {
        // Regex search - we need the editor context for this
        // This will be handled specially in the wrapper function
        return ADDR_NONE;
    }
    else
    {
        // Not a valid start of address
        return ADDR_NONE;
    }

apply_offset:
    if (relative || offset)
        base = base + sign * offset;

    // Validate range (1-based) - allow 0 through last_line
    // Note: 0 means "before first line" and may be valid for some operations
    if (base < 0 || base > last_line)
    {
        return ADDR_ERROR;
    }

    *pp = p;
    return base;
}

/* Parse leading address(es) from command line.
 * Returns pointer to first character AFTER the address part,
 * or NULL on syntax error.
 * Sets:
 *   addr1 → first address  (1-based, or 0 if none)
 *   addr2 → second address (1-based, or 0 if none)
 *   have_comma → true if range like "1,10" was seen
 */
const char *parse_ed_address(const char *line, int *addr1, int *addr2, bool *have_comma, int current_line,
                             int last_line, const int marks[26])
{
    const char *p = line;
    *addr1 = 0;
    *addr2 = 0;
    *have_comma = false;

    // Skip leading whitespace
    while (*p == ' ' || *p == '\t')
        p++;

    // Empty line or comment → no address
    if (*p == '\0' || *p == '#')
        return p;

    // First address (may be missing)
    int a1 = parse_one_address(&p, current_line, last_line, marks);
    if (a1 == ADDR_ERROR)
        return NULL;
    if (a1 == ADDR_NONE)
    {
        // No first address - check if there's a comma
        // If no comma, we're done (addr1 remains 0)
        // If comma, continue to parse range
        if (*p != ',')
        {
            return p;
        }
    }
    else
    {
        *addr1 = a1;
    }

    // Skip whitespace after first address
    while (*p == ' ' || *p == '\t')
        p++;

    // Optional comma + second address
    if (*p == ',')
    {
        p++;
        *have_comma = true;

        while (*p == ' ' || *p == '\t')
            p++;

        int a2 = parse_one_address(&p, current_line, last_line, marks);
        if (a2 == ADDR_ERROR)
            return NULL;
        if (a2 == ADDR_NONE)
        {
            // ",cmd" means "1,$cmd"
            if (*addr1 == 0)
                *addr1 = 1;
            *addr2 = last_line;
        }
        else
        {
            if (*addr1 == 0)
                *addr1 = 1;
            *addr2 = a2;
        }
    }
    else
    {
        // No comma → if there was a first address, default second = first
        if (*addr1 != 0)
            *addr2 = *addr1;
    }

    // Final whitespace skip
    while (*p == ' ' || *p == '\t')
        p++;
    return p; // points to command char (p,d,s,k, etc.) or semicolon or NUL
}

// Read an entire line from fp, handling arbitrarily long input.
// Returns a heap-allocated string WITHOUT the trailing newline.
// Sets *had_newline to 1 if a newline was consumed, 0 if EOF ended the line.
// Returns NULL if EOF encountered before any characters were read.
static char *read_full_line(FILE *fp, int *had_newline)
{
    if (had_newline)
        *had_newline = 0;
    size_t cap = 128;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return NULL;
    int c;
    while ((c = fgetc(fp)) != EOF)
    {
        if (c == '\n')
        {
            if (had_newline)
                *had_newline = 1;
            break;
        }
        if (c == '\r')
        { // Handle CRLF: peek next
            int next = fgetc(fp);
            if (next == '\n')
            {
                if (had_newline)
                    *had_newline = 1;
                break;
            }
            // Not CRLF, push back next
            if (next != EOF)
                ungetc(next, fp);
            // Treat '\r' as normal char
        }
        if (len + 1 >= cap)
        {
            size_t new_cap = cap * 2;
            char *new_buf = (char *)realloc(buf, new_cap);
            if (!new_buf)
            {
                free(buf);
                return NULL;
            }
            buf = new_buf;
            cap = new_cap;
        }
        buf[len++] = (char)c;
    }
    if (c == EOF && len == 0)
    {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static void emergency_save(Editor *ed)
{
    char filename[MAX_LINE];
    char *target = ed->filename;

    if (target == NULL)
    {
        PRINTF("Enter filename to save: ");
        if (fgets(filename, MAX_LINE, input_fp ? input_fp : stdin) == NULL)
        {
            PRINTF("Save failed.\n");
            return;
        }
        filename[strcspn(filename, "\n")] = 0;
        target = filename;
    }

    FILE *fp = fopen(target, "w");
    if (!fp)
    {
        PRINTF("Could not open %s for writing.\n", target);
        if (ed->filename == NULL)
        {
            PRINTF("Enter alternate filename: ");
            if (fgets(filename, MAX_LINE, input_fp ? input_fp : stdin) == NULL)
            {
                PRINTF("Save failed.\n");
                return;
            }
            filename[strcspn(filename, "\n")] = 0;
            fp = fopen(filename, "w");
            if (!fp)
            {
                PRINTF("Could not save buffer.\n");
                return;
            }
        }
        else
        {
            return;
        }
    }

    int bytes = 0;
    for (int i = 0; i < ed->num_lines; i++)
    {
        fprintf(fp, "%s\n", ed->lines[i]);
        bytes += strlen(ed->lines[i]) + 1;
    }
    fclose(fp);
    PRINTF("Saved %d bytes.\n", bytes);
}

static void critical_error(Editor *ed)
{
    PRINTF("\n*** CRITICAL ERROR: Memory allocation failed ***\n");
    PRINTF("The program must exit. Save current buffer? (Y/N): ");
    char response[MAX_LINE];
    if (fgets(response, MAX_LINE, input_fp ? input_fp : stdin) == NULL)
    {
        exit(1);
    }
    response[strcspn(response, "\n")] = 0;
    if (response[0] == 'Y' || response[0] == 'y')
    {
        emergency_save(ed);
    }
    exit(1);
}

void init_editor(Editor *ed)
{
    ed->lines = NULL;
    ed->num_lines = 0;
    ed->current_line = -1; // 0-indexed: -1 means before first line
    ed->dirty = 0;
    ed->filename = NULL;
    ed->verbose = 0;
    ed->last_error = NULL;
    for (int i = 0; i < 26; i++)
        ed->marks[i] = -1;
    ed->prompt = 0;
    ed->undo_lines = NULL;
    ed->undo_num_lines = 0;
    ed->undo_current_line = 0;
    ed->undo_valid = 0;
}

// Legacy parse_address function - kept for backward compatibility
// Returns 0-based line number or -1 on error
int parse_address(Editor *ed, const char *addr)
{
    if (!addr || !*addr)
        return ed->current_line;

    // Trim leading/trailing spaces
    while (*addr == ' ' || *addr == '\t')
        addr++;
    if (*addr == '\0')
        return ed->current_line;

    // Special case: handle regex addresses /pattern/ or ?pattern?
    if (*addr == '/' || *addr == '?')
    {
        char delim = *addr;
        const char *p = addr + 1;
        char pattern[MAX_LINE];
        size_t i = 0;

        // Extract pattern
        while (*p && *p != delim && i < sizeof(pattern) - 1)
        {
            pattern[i++] = *p++;
        }
        if (*p != delim)
            return -1; // Unterminated pattern
        pattern[i] = '\0';
        p++; // Skip closing delimiter

        // Search for pattern
        int found = search_pattern(ed, pattern, i, delim == '/');
        if (found == 0)
            return -1; // Not found

        // Handle optional offset after closing delimiter
        if (*p)
        {
            // Parse offset (e.g., "+1" or "-2")
            const char *offset_p = p;
            int current_1based = found; // found is already 1-based from search_pattern
            int last_1based = ed->num_lines;
            int marks_unused[26];
            for (int j = 0; j < 26; j++)
                marks_unused[j] = -1;

            int offset_result = parse_one_address(&offset_p, current_1based, last_1based, marks_unused);
            if (offset_result == ADDR_ERROR)
                return -1;
            if (offset_result != ADDR_NONE)
            {
                found = offset_result; // Use the offset result (1-based)
            }
        }

        // Validate and convert to 0-based
        if (found <= 0 || found > ed->num_lines)
            return -1;
        return found - 1; // Convert from 1-based to 0-based
    }

    // Use the new parser for non-regex addresses
    const char *p = addr;
    int current_1based = (ed->current_line >= 0) ? ed->current_line + 1 : 1;
    int last_1based = ed->num_lines;

    int result = parse_one_address(&p, current_1based, last_1based, ed->marks);

    if (result == ADDR_ERROR || result == ADDR_NONE)
    {
        return -1;
    }

    // Line 0 means "before first line" - invalid for operations that reference existing lines
    if (result == 0)
    {
        return -1;
    }

    // Convert from 1-based to 0-based
    if (result > 0)
        result--;

    // Validate
    if (result < 0 || result >= ed->num_lines)
    {
        return -1;
    }

    return result;
}

// Parse address range: [addr1[,addr2]]
// Returns AddressRange with start/end (0-based indices)
// If no addresses: defaults to current line
// If one address: start == end (single line)
// If addr1,addr2: range from addr1 to addr2 inclusive
// Special case: "," alone means 1,$  (all lines)
// Parse address range using new parser
// Returns AddressRange with start/end (0-based indices)
AddressRange parse_address_range(Editor *ed, const char *range_str)
{
    AddressRange result = {-1, -1};

    if (!range_str)
        range_str = "";

    int addr1 = 0, addr2 = 0;
    bool have_comma = false;

    // Convert from 0-based to 1-based for parser
    int current_1based = (ed->current_line >= 0) ? ed->current_line + 1 : 1;
    int last_1based = ed->num_lines;

    const char *after =
        parse_ed_address(range_str, &addr1, &addr2, &have_comma, current_1based, last_1based, ed->marks);

    if (!after)
    {
        // Parse error
        return result;
    }

    // Handle empty address (no address given)
    if (addr1 == 0 && addr2 == 0)
    {
        // Default to current line
        if (ed->num_lines > 0 && ed->current_line >= 0)
        {
            result.start = ed->current_line;
            result.end = ed->current_line;
        }
        return result;
    }

    // Convert from 1-based to 0-based
    if (addr1 > 0)
        addr1--;
    if (addr2 > 0)
        addr2--;

    // Validate range
    if (addr1 >= 0 && addr1 < ed->num_lines && addr2 >= 0 && addr2 < ed->num_lines && addr1 <= addr2)
    {
        result.start = addr1;
        result.end = addr2;
    }

    return result;
}

void append_line(Editor *ed, int addr)
{
    prepare_undo(ed);
    int original_num_lines = ed->num_lines;
    int first_insert_pos = addr + 1; // Where first line will be inserted (0-indexed)
    int last_inserted_index = -1;
    int num_inserted = 0;
    if (!input_fp)
        PRINTF("(Enter text, end with '.' on a new line)\n");
    while (1)
    {
        int had_nl = 0;
        char *line = read_full_line(input_fp ? input_fp : stdin, &had_nl);
        if (!line)
            break; // EOF
        if (strcmp(line, ".") == 0)
        {
            free(line);
            break;
        }
        // Insert after addr: result position is addr+1
        char **new_lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        if (new_lines == NULL)
        {
            free(line);
            critical_error(ed);
        }
        ed->lines = new_lines;
        for (int i = ed->num_lines; i > addr + 1; i--)
        {
            ed->lines[i] = ed->lines[i - 1];
        }
        ed->lines[addr + 1] = line; // already allocated
        ed->num_lines++;
        addr++;
        last_inserted_index = addr;
        num_inserted++;
        if (!had_nl)
            break; // Last line without newline (EOF mid-line)
    }
    if (last_inserted_index >= 0)
    {
        ed->current_line = last_inserted_index; // 0-indexed
    }
    else
    {
        // No text appended - set current to where first line would have been inserted
        // For append after addr, that's addr+1 (or addr if buffer would still be empty)
        if (ed->num_lines > 0 && addr + 1 < ed->num_lines)
        {
            ed->current_line = addr + 1;
        }
        else if (ed->num_lines > 0)
        {
            ed->current_line = addr; // At end of buffer
        }
        // If buffer is empty, leave current_line as is (probably -1 or 0)
    }
    if (ed->num_lines > original_num_lines)
    {
        ed->dirty = 1;
        // Update marks after insertion
        update_marks_after_insert(ed, first_insert_pos, num_inserted);
    }
}

void insert_line(Editor *ed, int addr)
{
    prepare_undo(ed);
    int original_num_lines = ed->num_lines;
    int first_insert_pos = addr; // Where first line will be inserted (0-indexed)
    int last_inserted_index = -1;
    int num_inserted = 0;
    if (!input_fp)
        PRINTF("(Enter text, end with '.' on a new line)\n");
    while (1)
    {
        int had_nl = 0;
        char *line = read_full_line(input_fp ? input_fp : stdin, &had_nl);
        if (!line)
            break; // EOF
        if (strcmp(line, ".") == 0)
        {
            free(line);
            break;
        }
        char **new_lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        if (new_lines == NULL)
        {
            free(line);
            critical_error(ed);
        }
        ed->lines = new_lines;
        for (int i = ed->num_lines; i > addr; i--)
        {
            ed->lines[i] = ed->lines[i - 1];
        }
        ed->lines[addr] = line;
        ed->num_lines++;
        last_inserted_index = addr;
        addr++;
        num_inserted++;
        if (!had_nl)
            break;
    }
    if (last_inserted_index >= 0)
    {
        ed->current_line = last_inserted_index; // 0-indexed
    }
    else
    {
        // No text inserted, but set current to where it would have been inserted
        ed->current_line = addr;
    }
    if (ed->num_lines > original_num_lines)
    {
        ed->dirty = 1;
        // Update marks after insertion
        update_marks_after_insert(ed, first_insert_pos, num_inserted);
    }
}

void print_line(Editor *ed, int addr)
{
    if (ed->num_lines == 0 || addr < 0 || addr >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }
    PRINTF("%s\n", ed->lines[addr]);
    ed->current_line = addr; // 0-indexed
}

// Print range of lines
void print_range(Editor *ed, AddressRange range)
{
    if (range.start < 0 || range.end < 0)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }
    for (int i = range.start; i <= range.end; i++)
    {
        PRINTF("%s\n", ed->lines[i]);
    }
    ed->current_line = range.end; // 0-indexed
}

// Print range with line numbers (n command)
void print_numbered_range(Editor *ed, AddressRange range)
{
    if (range.start < 0 || range.end < 0)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }
    for (int i = range.start; i <= range.end; i++)
    {
        PRINTF("%d\t%s\n", i + 1, ed->lines[i]); // Display 1-based to user
    }
    ed->current_line = range.end; // 0-indexed
}

// Print range unambiguously (l command) - show special chars
void print_list_range(Editor *ed, AddressRange range)
{
    if (range.start < 0 || range.end < 0)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }
    for (int i = range.start; i <= range.end; i++)
    {
        const char *line = ed->lines[i];
        for (size_t j = 0; j < strlen(line); j++)
        {
            unsigned char c = (unsigned char)line[j];
            if (c == '\\')
                PRINTF("\\\\");
            else if (c == '\t')
                PRINTF("\\t");
            else if (c == '\b')
                PRINTF("\\b");
            else if (c == '\f')
                PRINTF("\\f");
            else if (c == '\r')
                PRINTF("\\r");
            else if (c == '\v')
                PRINTF("\\v");
            else if (c < 32 || c >= 127)
                PRINTF("\\%03o", c);
            else
                putchar(c);
        }
        PRINTF("$\n");
    }
    ed->current_line = range.end; // 0-indexed
}

void delete_line(Editor *ed, int addr)
{
    prepare_undo(ed);
    if (ed->num_lines == 0 || addr < 0 || addr >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }
    free(ed->lines[addr]);
    for (int i = addr; i < ed->num_lines - 1; i++)
    {
        ed->lines[i] = ed->lines[i + 1];
    }
    ed->num_lines--;
    if (ed->num_lines == 0)
    {
        free(ed->lines);
        ed->lines = NULL;
    }
    else
    {
        char **new_lines = realloc(ed->lines, ed->num_lines * sizeof(char *));
        if (new_lines == NULL)
        {
            critical_error(ed);
        }
        ed->lines = new_lines;
    }
    // Set current to next line, or last line if at end (0-indexed)
    ed->current_line = (addr < ed->num_lines) ? addr : ed->num_lines - 1;
    if (ed->current_line < 0)
        ed->current_line = -1; // Empty buffer
    // Line was successfully deleted, so set dirty flag
    ed->dirty = 1;
    // Update marks after deletion
    update_marks_after_delete(ed, addr, 1);
}

// Delete range of lines
void delete_range(Editor *ed, AddressRange range)
{
    prepare_undo(ed);
    if (range.start < 0 || range.end < 0)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }

    // Free lines in range
    for (int i = range.start; i <= range.end; i++)
    {
        free(ed->lines[i]);
    }

    // Shift remaining lines down
    int num_deleted = range.end - range.start + 1;
    for (int i = range.start; i < ed->num_lines - num_deleted; i++)
    {
        ed->lines[i] = ed->lines[i + num_deleted];
    }

    ed->num_lines -= num_deleted;

    if (ed->num_lines == 0)
    {
        free(ed->lines);
        ed->lines = NULL;
        ed->current_line = -1; // Empty buffer (0-indexed)
    }
    else
    {
        char **new_lines = realloc(ed->lines, ed->num_lines * sizeof(char *));
        if (new_lines == NULL)
        {
            critical_error(ed);
        }
        ed->lines = new_lines;
        // Set current line to line after deleted range, or last line if at end (0-indexed)
        ed->current_line = (range.start < ed->num_lines) ? range.start : ed->num_lines - 1;
    }

    ed->dirty = 1;
    // Update marks after deletion
    update_marks_after_delete(ed, range.start, num_deleted);
}

void write_file(Editor *ed, const char *filename)
{
    const char *target = filename;
    if (target == NULL || strlen(target) == 0)
    {
        target = ed->filename;
    }
    if (target == NULL || strlen(target) == 0)
    {
        set_error(ed, "No current filename");
        return;
    }

    FILE *fp = fopen(target, "w");
    if (!fp)
    {
        set_error(ed, "Write failed");
        return;
    }
    int bytes = 0;
    for (int i = 0; i < ed->num_lines; i++)
    {
        fprintf(fp, "%s\n", ed->lines[i]);
        bytes += strlen(ed->lines[i]) + 1; // Include newline
    }
    fclose(fp);
    PRINTF("%d\n", bytes);
    ed->dirty = 0;

    // Update filename if a new one was specified
    if (filename != NULL && strlen(filename) > 0)
    {
        if (ed->filename)
        {
            free(ed->filename);
        }
        ed->filename = my_strdup(filename);
        if (ed->filename == NULL)
        {
            critical_error(ed);
        }
    }
}

void free_editor(Editor *ed)
{
    if (!ed)
        return;
    for (int i = 0; i < ed->num_lines; i++)
    {
        free(ed->lines[i]);
    }
    free(ed->lines);
    ed->lines = NULL;
    free(ed->filename);
    ed->filename = NULL;
    if (ed->last_error)
        free(ed->last_error);
    ed->last_error = NULL;
    if (ed->undo_lines)
    {
        for (int i = 0; i < ed->undo_num_lines; i++)
            free(ed->undo_lines[i]);
        free(ed->undo_lines);
        ed->undo_lines = NULL;
    }
    ed->num_lines = 0;
    ed->current_line = 0;
    ed->dirty = 0;
    ed->verbose = 0;
    ed->undo_num_lines = 0;
    ed->undo_current_line = 0;
    ed->undo_valid = 0;
    for (int i = 0; i < 26; i++)
        ed->marks[i] = -1;
}

#ifndef LED_TEST
static void execute_command(Editor *ed, const char *cmd)
{
    // Work on a local mutable copy to avoid modifying string literals
    char cmd_buf[MAX_LINE];
    strncpy(cmd_buf, cmd ? cmd : "", sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';

    // Remove trailing newline if present
    cmd_buf[strcspn(cmd_buf, "\n")] = '\0';
    if (cmd_buf[0] == '\0')
        return;

    // Use the new parser to find where the address portion ends
    int addr1 = 0, addr2 = 0;
    bool have_comma = false;
    int current_1based = (ed->current_line >= 0) ? ed->current_line + 1 : 1;
    int last_1based = ed->num_lines;

    const char *cmd_start =
        parse_ed_address(cmd_buf, &addr1, &addr2, &have_comma, current_1based, last_1based, ed->marks);

    char addr_str[MAX_LINE] = "";
    char op = '\0';

    if (!cmd_start)
    {
        // Parse error in address
        set_error(ed, "Invalid address");
        return;
    }

    // Extract address portion for later use
    if (cmd_start > cmd_buf)
    {
        size_t addr_len = cmd_start - cmd_buf;
        if (addr_len >= sizeof(addr_str))
            addr_len = sizeof(addr_str) - 1;
        strncpy(addr_str, cmd_buf, addr_len);
        addr_str[addr_len] = '\0';
    }

    // Get the command character
    if (*cmd_start)
    {
        op = *cmd_start;
    }
    else
    {
        op = '\0';
    }

    // Bare address: just set current line (0-indexed)
    if (op == '\0')
    {
        // No command, just address - set current line
        if (addr1 > 0)
        {
            int addr = addr1 - 1; // Convert from 1-based to 0-based
            if (addr >= 0 && addr < ed->num_lines)
            {
                ed->current_line = addr;
            }
            else
            {
                set_error(ed, "Invalid address");
            }
        }
        return;
    }

    // Global commands: [addr[,addr]]g/pat/command or v/pat/command
    if ((op == 'g' || op == 'v') && cmd_start[1] == '/')
    {
        int is_g = (op == 'g');
        const char *p = cmd_start + 2; // after g/ or v/
        char pat[MAX_LINE] = {0};
        size_t i = 0;
        while (*p && *p != '/' && i < sizeof(pat) - 1)
            pat[i++] = *p++;
        if (*p != '/')
        {
            set_error(ed, "Invalid global");
            return;
        }
        pat[i] = '\0';
        p++; // skip closing '/'
        // Skip leading spaces before inner command
        while (*p == ' ')
            p++;
        const char *inner = p; // command or '{' to begin a list
        if (!*inner)
        {
            set_error(ed, "Invalid global");
            return;
        }

        // Build range from parsed addresses
        AddressRange r;
        if (addr1 == 0 && addr2 == 0)
        {
            // No address given - default to all lines
            r.start = 0;
            r.end = ed->num_lines - 1;
        }
        else
        {
            // Convert from 1-based to 0-based
            r.start = (addr1 > 0) ? addr1 - 1 : 0;
            r.end = (addr2 > 0) ? addr2 - 1 : ed->num_lines - 1;
        }

        if (r.start < 0 || r.end < 0 || r.start >= ed->num_lines || r.end >= ed->num_lines)
        {
            set_error(ed, "Invalid address");
            return;
        }
        // Collect target line indices first
        int cap = (r.end - r.start + 1);
        int *idxs = (int *)malloc(cap * sizeof(int));
        if (!idxs)
            critical_error(ed);
        int n = 0;
        BreMatch m;
        for (int i2 = r.start; i2 <= r.end; i2++)
        {
            bool matched = bre_match(ed->lines[i2], pat, &m) == BRE_OK;
            if ((is_g && matched) || (!is_g && !matched))
                idxs[n++] = i2;
        }
        // If inner begins a brace-enclosed list, read commands until a line with only '}'
        if (inner[0] == '{' && inner[1] == '\0')
        {
            // Read command list lines from input source
            size_t list_cap = 8, list_len = 0;
            char **list = (char **)malloc(list_cap * sizeof(char *));
            if (!list)
            {
                free(idxs);
                critical_error(ed);
            }
            char linebuf[MAX_LINE];
            while (fgets(linebuf, sizeof(linebuf), input_fp ? input_fp : stdin))
            {
                // Trim trailing newline
                size_t lb = strcspn(linebuf, "\r\n");
                linebuf[lb] = '\0';
                // Trim leading/trailing spaces for '}' detection
                char *s = linebuf;
                while (*s == ' ')
                    s++;
                char *e = s + strlen(s);
                while (e > s && (e[-1] == ' '))
                {
                    e--;
                }
                *e = '\0';
                if (strcmp(s, "}") == 0)
                    break;
                // Store the trimmed command line
                char *stored = my_strdup(s);
                if (!stored)
                {
                    for (size_t qi = 0; qi < list_len; qi++)
                        free(list[qi]);
                    free(list);
                    free(idxs);
                    critical_error(ed);
                }
                if (list_len == list_cap)
                {
                    size_t new_cap = list_cap * 2;
                    char **new_list = (char **)realloc(list, new_cap * sizeof(char *));
                    if (!new_list)
                    {
                        for (size_t qi = 0; qi < list_len; qi++)
                            free(list[qi]);
                        free(list);
                        free(idxs);
                        critical_error(ed);
                    }
                    list = new_list;
                    list_cap = new_cap;
                }
                list[list_len++] = stored;
            }
            // Execute commands for each stored index in descending order to reduce reindexing issues
            for (int k = n - 1; k >= 0; k--)
            {
                int target = idxs[k]; // 0-indexed
                if (ed->num_lines == 0)
                    continue;
                if (target < 0)
                    target = 0;
                if (target >= ed->num_lines)
                    target = ed->num_lines - 1;
                ed->current_line = target; // 0-indexed
                size_t ci = 0;
                while (ci < list_len)
                {
                    const char *cmdline = list[ci];
                    if (!cmdline || cmdline[0] == '\0')
                    {
                        ci++;
                        continue;
                    }

                    // Check if this is an insert or append command
                    // Simple detection: check if command (after optional address) is 'i' or 'a'
                    char first_non_addr = '\0';
                    const char *p = cmdline;
                    // Skip leading whitespace
                    while (*p == ' ' || *p == '\t')
                        p++;
                    // Skip address characters
                    while (*p && (isdigit(*p) || *p == '.' || *p == '$' || *p == '+' || *p == '-' || *p == ',' ||
                                  *p == '\'' || (*p >= 'a' && *p <= 'z' && *(p - 1) == '\'')))
                        p++;
                    // Skip whitespace after address
                    while (*p == ' ' || *p == '\t')
                        p++;
                    first_non_addr = *p;

                    if (first_non_addr == 'i' || first_non_addr == 'a')
                    {
                        // This is insert/append - collect text lines until '.'
                        // Build content: command + newline + text lines + '.\n'
                        char *combined = (char *)malloc(MAX_LINE * 64);
                        if (!combined)
                        {
                            ci++;
                            continue;
                        }
                        combined[0] = '\0';

                        // Start with the command itself
                        strncat(combined, cmdline, MAX_LINE * 64 - 1);
                        strncat(combined, "\n", MAX_LINE * 64 - strlen(combined) - 1);
                        size_t cmd_end = strlen(combined);

                        ci++; // Move to next line (first text line or '.')
                        while (ci < list_len)
                        {
                            const char *textline = list[ci];
                            strncat(combined, textline, MAX_LINE * 64 - strlen(combined) - 1);
                            strncat(combined, "\n", MAX_LINE * 64 - strlen(combined) - 1);
                            if (strcmp(textline, ".") == 0)
                            {
                                ci++; // Consume the '.'
                                break;
                            }
                            ci++;
                        }

                        // Now create a temporary file with the text content (not including command line)
                        FILE *saved_input = input_fp;
                        FILE *temp_input = tmpfile();
                        if (temp_input)
                        {
                            // Write only the text lines (after the command)
                            fputs(combined + cmd_end, temp_input);
                            rewind(temp_input);
                            input_fp = temp_input;
                            // Execute command
                            char tmp[MAX_LINE];
                            strncpy(tmp, cmdline, sizeof(tmp) - 1);
                            tmp[sizeof(tmp) - 1] = '\0';
                            execute_command(ed, tmp);
                            fclose(temp_input);
                            input_fp = saved_input;
                        }
                        free(combined);
                    }
                    else
                    {
                        // Regular command - execute as-is
                        char tmp[MAX_LINE];
                        strncpy(tmp, cmdline, sizeof(tmp) - 1);
                        tmp[sizeof(tmp) - 1] = '\0';
                        execute_command(ed, tmp);
                        ci++;
                    }
                }
            }
            for (size_t qi = 0; qi < list_len; qi++)
                free(list[qi]);
            free(list);
            free(idxs);
            return;
        }
        else
        {
            // Single inner command: execute for each stored index; prefix numeric address
            for (int k = n - 1; k >= 0; k--)
            {
                int line_no = idxs[k] + 1; // Convert to 1-based for command prefix
                char buf[MAX_LINE * 2];
                if (inner[0] == 'g' || inner[0] == 'v')
                {
                    // Do not prefix nested global; execute as-is
                    char tmp[MAX_LINE * 2];
                    strncpy(tmp, inner, sizeof(tmp) - 1);
                    tmp[sizeof(tmp) - 1] = '\0';
                    // Set current line to target for relative commands inside nested global (0-indexed)
                    if (ed->num_lines > 0)
                    {
                        int target = idxs[k];
                        if (target < 0)
                            target = 0;
                        if (target >= ed->num_lines)
                            target = ed->num_lines - 1;
                        ed->current_line = target; // 0-indexed
                    }
                    execute_command(ed, tmp);
                }
                else if (inner[0] == 's')
                {
                    snprintf(buf, sizeof(buf), "%d%s", line_no, inner);
                    execute_command(ed, buf);
                }
                else
                {
                    snprintf(buf, sizeof(buf), "%d%s", line_no, inner);
                    execute_command(ed, buf);
                }
            }
            free(idxs);
            return;
        }
    }

    // Substitution: [addr[,addr]]s/pattern/replacement/[g]
    if (op == 's' && cmd_start[1] == '/')
    {
        const char *p = cmd_start + 2; // after s/
        char pattern[MAX_LINE] = {0};
        char replacement[MAX_LINE] = {0};
        size_t i = 0;
        while (*p && *p != '/' && i < sizeof(pattern) - 1)
        {
            pattern[i++] = *p++;
        }
        if (*p != '/')
        {
            set_error(ed, "Invalid substitute");
            return;
        }
        pattern[i] = '\0';
        p++; // skip '/'
        i = 0;
        while (*p && *p != '/' && i < sizeof(replacement) - 1)
        {
            replacement[i++] = *p++;
        }
        if (*p != '/')
        {
            set_error(ed, "Invalid substitute");
            return;
        }
        replacement[i] = '\0';
        p++; // skip '/'
        int global = 0;
        while (*p)
        {
            if (*p == 'g')
                global = 1;
            else
            {
                set_error(ed, "Invalid flag");
                return;
            }
            p++;
        }

        // Build range from parsed addresses
        AddressRange range;
        if (addr1 == 0 && addr2 == 0)
        {
            // No address - default to current line
            if (ed->num_lines > 0 && ed->current_line >= 0)
            {
                range.start = ed->current_line;
                range.end = ed->current_line;
            }
            else
            {
                set_error(ed, "Invalid address");
                return;
            }
        }
        else
        {
            range.start = (addr1 > 0) ? addr1 - 1 : 0;
            range.end = (addr2 > 0) ? addr2 - 1 : addr1 - 1;
        }

        if (range.start < 0 || range.end < 0 || range.start >= ed->num_lines || range.end >= ed->num_lines)
        {
            set_error(ed, "Invalid address");
            return;
        }
        substitute_range(ed, range, pattern, replacement, global);
        return;
    }

    // Mark command: [addr]k<x>
    if (op == 'k' && cmd_start[1] >= 'a' && cmd_start[1] <= 'z')
    {
        int addrk;
        if (addr1 == 0)
        {
            // No address given, use current line
            addrk = ed->current_line;
        }
        else
        {
            // Convert from 1-based to 0-based
            addrk = addr1 - 1;
        }
        if (addrk < 0 || addrk >= ed->num_lines)
        {
            set_error(ed, "Invalid address");
            return;
        }
        ed->marks[cmd_start[1] - 'a'] = addrk; // Store 0-indexed
        ed->current_line = addrk;              // 0-indexed
        return;
    }

    // Special handling for '=' command (print line number)
    if (op == '=')
    {
        if (strlen(addr_str) == 0)
        {
            // No address: print last line number
            PRINTF("%d\n", ed->num_lines);
        }
        else
        {
            int addr = parse_address(ed, addr_str);
            if (addr >= 0 && addr < ed->num_lines)
            {
                PRINTF("%d\n", addr + 1); // Display 1-based to user
            }
            else
            {
                set_error(ed, "Invalid address");
            }
        }
        return;
    }

    // Special handling for 'f' command (filename)
    if (op == 'f')
    {
        char *space = strchr(cmd_buf, ' ');
        if (space != NULL)
        {
            // Set new filename
            char *filename = space + 1;
            while (*filename == ' ')
                filename++;
            if (strlen(filename) > 0)
            {
                if (ed->filename)
                    free(ed->filename);
                ed->filename = my_strdup(filename);
                if (ed->filename == NULL)
                    critical_error(ed);
            }
        }
        // Display current filename
        if (ed->filename)
        {
            PRINTF("%s\n", ed->filename);
        }
        else
        {
            set_error(ed, "No current filename");
        }
        return;
    }

    // Special handling for 'w' command which may have a filename
    if (op == 'w')
    {
        char *space = strchr(cmd_buf, ' ');
        if (space != NULL)
        {
            char *filename = space + 1;
            while (*filename == ' ')
                filename++;
            if (strlen(filename) > 0)
            {
                write_file(ed, filename);
                return;
            }
        }
        write_file(ed, NULL);
        return;
    }

    // Special handling for 'W' command (write append) - requires filename
    if (op == 'W')
    {
        char *space = strchr(cmd_buf, ' ');
        if (space == NULL)
        {
            set_error(ed, "No filename specified");
            return;
        }
        char *filename = space + 1;
        while (*filename == ' ')
            filename++;
        if (strlen(filename) == 0)
        {
            set_error(ed, "No filename specified");
            return;
        }

        // Build range from parsed addresses
        AddressRange range;
        if (addr1 == 0 && addr2 == 0)
        {
            // No address - default to all lines
            range.start = 0;
            range.end = ed->num_lines - 1;
        }
        else
        {
            range.start = (addr1 > 0) ? addr1 - 1 : 0;
            range.end = (addr2 > 0) ? addr2 - 1 : addr1 - 1;
        }

        if (range.start < 0 || range.end < 0 || range.start >= ed->num_lines || range.end >= ed->num_lines)
        {
            set_error(ed, "Invalid address");
            return;
        }
        write_append_file(ed, range, filename);
        return;
    }

    // Special handling for 'e' command (edit with dirty check)
    if (op == 'e')
    {
        char *space = strchr(cmd_buf, ' ');
        const char *filename = NULL;
        if (space != NULL)
        {
            filename = space + 1;
            while (*filename == ' ')
                filename++;
            if (strlen(filename) == 0)
                filename = NULL;
        }
        if (filename == NULL)
            filename = ed->filename;
        if (filename == NULL)
        {
            set_error(ed, "No current filename");
            return;
        }
        edit_file(ed, filename);
        return;
    }

    // Special handling for 'E' command (forced edit)
    if (op == 'E')
    {
        char *space = strchr(cmd_buf, ' ');
        const char *filename = NULL;
        if (space != NULL)
        {
            filename = space + 1;
            while (*filename == ' ')
                filename++;
            if (strlen(filename) == 0)
                filename = NULL;
        }
        if (filename == NULL)
            filename = ed->filename;
        if (filename == NULL)
        {
            set_error(ed, "No current filename");
            return;
        }
        forced_edit_file(ed, filename);
        return;
    }

    // Special handling for 'r' command (read file) - requires filename
    if (op == 'r')
    {
        char *space = strchr(cmd_buf, ' ');
        if (space == NULL)
        {
            set_error(ed, "No filename specified");
            return;
        }
        char *filename = space + 1;
        while (*filename == ' ')
            filename++;
        if (strlen(filename) == 0)
        {
            set_error(ed, "No filename specified");
            return;
        }
        int addr;
        if (addr1 == 0)
        {
            addr = ed->num_lines - 1; // Default to end of buffer (0-indexed)
        }
        else
        {
            addr = addr1 - 1; // Convert from 1-based to 0-based
        }
        read_file_at_address(ed, addr, filename);
        return;
    }

    // Special handling for 'm' and 't' commands (move/copy) - need destination address
    if (op == 'm' || op == 't')
    {
        char *space = strchr(cmd_buf, ' ');
        if (space == NULL)
        {
            set_error(ed, "No destination address");
            return;
        }
        char *dest_str = space + 1;
        while (*dest_str == ' ')
            dest_str++;
        if (strlen(dest_str) == 0)
        {
            set_error(ed, "No destination address");
            return;
        }

        // Build range from parsed addresses
        AddressRange range;
        if (addr1 == 0 && addr2 == 0)
        {
            // No address - default to current line
            if (ed->num_lines > 0 && ed->current_line >= 0)
            {
                range.start = ed->current_line;
                range.end = ed->current_line;
            }
            else
            {
                set_error(ed, "Invalid address");
                return;
            }
        }
        else
        {
            range.start = (addr1 > 0) ? addr1 - 1 : 0;
            range.end = (addr2 > 0) ? addr2 - 1 : addr1 - 1;
        }

        if (range.start < 0 || range.end < 0 || range.start >= ed->num_lines || range.end >= ed->num_lines)
        {
            set_error(ed, "Invalid address");
            return;
        }

        int dest = parse_address(ed, dest_str);
        if (dest < 0)
        {
            set_error(ed, "Invalid destination");
            return;
        }

        if (op == 'm')
        {
            move_range(ed, range, dest);
        }
        else
        {
            copy_range(ed, range, dest);
        }
        return;
    }

    // Commands that support ranges
    if (op == 'p' || op == 'd' || op == 'n' || op == 'l' || op == 'c' || op == 'j')
    {
        // Build range from parsed addresses
        AddressRange range;
        if (addr1 == 0 && addr2 == 0)
        {
            // No address - default to current line
            if (ed->num_lines > 0 && ed->current_line >= 0)
            {
                range.start = ed->current_line;
                range.end = ed->current_line;
            }
            else
            {
                set_error(ed, "Invalid address");
                return;
            }
        }
        else
        {
            range.start = (addr1 > 0) ? addr1 - 1 : 0;
            range.end = (addr2 > 0) ? addr2 - 1 : addr1 - 1;
        }

        if (range.start < 0 || range.end < 0 || range.start >= ed->num_lines || range.end >= ed->num_lines)
        {
            set_error(ed, "Invalid address");
            return;
        }

        switch (op)
        {
        case 'p':
            print_range(ed, range);
            break;
        case 'd':
            delete_range(ed, range);
            break;
        case 'n':
            print_numbered_range(ed, range);
            break;
        case 'l':
            print_list_range(ed, range);
            break;
        case 'c':
            change_range(ed, range);
            break;
        case 'j':
            join_range(ed, range);
            break;
        }
        return;
    }

    // Commands that need single address
    int addr;
    if (addr1 == 0)
    {
        // No address given - use current line as default
        addr = ed->current_line;
    }
    else
    {
        addr = addr1 - 1; // Convert from 1-based to 0-based
    }

    if (addr < 0 && op != 'a' && op != 'i' && op != 'q' && op != 'Q' && op != 'H' && op != 'h')
    {
        set_error(ed, "Invalid address");
        return;
    }

    switch (op)
    {
    case 'a':
        append_line(ed, addr < 0 ? (ed->num_lines > 0 ? ed->num_lines - 1 : -1) : addr);
        break;
    case 'i':
        insert_line(ed, addr < 0 ? 0 : addr);
        break;
    case 'u':
        if (!ed->undo_valid)
        {
            set_error(ed, "Nothing to undo");
            break;
        }
        // Swap current buffer with undo snapshot
        {
            // Free current lines
            for (int i = 0; i < ed->num_lines; i++)
                free(ed->lines[i]);
            free(ed->lines);
            ed->lines = ed->undo_lines;
            ed->num_lines = ed->undo_num_lines;
            ed->current_line = ed->undo_current_line;
            ed->undo_lines = NULL;
            ed->undo_num_lines = 0;
            ed->undo_current_line = 0;
            ed->undo_valid = 0;
            ed->dirty = 1;
        }
        break;
    case 'q':
        if (ed->dirty)
            set_error(ed, "Buffer modified");
        else
            exit(0);
        break;
    case 'Q':
        exit(0);
        break;
    case 'H':
        ed->verbose = !ed->verbose;
        PRINTF("Verbose %s\n", ed->verbose ? "on" : "off");
        break;
    case 'P':
        ed->prompt = !ed->prompt;
        PRINTF("Prompt %s\n", ed->prompt ? "on" : "off");
        break;
    case 'h':
        if (ed->last_error)
            PRINTF("%s\n", ed->last_error);
        else
            PRINTF("No error\n");
        break;
    default:
        set_error(ed, "Unknown command");
    }
}
#endif

// New function to load a file into the editor buffer
void load_file(Editor *ed, const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
    {
        set_error(ed, "Cannot open file");
        return;
    }
    int bytes = 0;
    while (1)
    {
        int had_nl = 0;
        char *line = read_full_line(fp, &had_nl);
        if (!line)
            break;
        char **new_lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        if (new_lines == NULL)
        {
            free(line);
            fclose(fp);
            critical_error(ed);
        }
        ed->lines = new_lines;
        ed->lines[ed->num_lines] = line;
        ed->num_lines++;
        bytes += (int)strlen(line) + (had_nl ? 1 : 0);
        if (!had_nl)
            break; // EOF mid-line
    }
    fclose(fp);
    ed->current_line = ed->num_lines - 1; // 0-indexed: last line
    if (ed->current_line < 0)
        ed->current_line = -1; // Empty buffer
    PRINTF("%d\n", bytes);
    if (ed->filename)
        free(ed->filename);
    ed->filename = my_strdup(filename);
    if (ed->filename == NULL)
        critical_error(ed);
}

// Edit command: load file after checking dirty flag
void edit_file(Editor *ed, const char *filename)
{
    if (ed->dirty)
    {
        set_error(ed, "Buffer modified");
        return;
    }
    // Clear current buffer
    prepare_undo(ed);
    free_editor(ed);
    init_editor(ed);
    // Load new file
    load_file(ed, filename);
}

// Forced edit command: load file without dirty check
void forced_edit_file(Editor *ed, const char *filename)
{
    // Clear current buffer
    prepare_undo(ed);
    free_editor(ed);
    init_editor(ed);
    // Load new file
    load_file(ed, filename);
}

// Read command: insert file contents after specified address
void read_file_at_address(Editor *ed, int addr, const char *filename)
{
    prepare_undo(ed);
    FILE *fp = fopen(filename, "r");
    if (!fp)
    {
        set_error(ed, "Cannot open file");
        return;
    }

    int bytes = 0;
    int insert_pos = addr + 1; // Insert after addr
    int first_insert_pos = insert_pos; // Remember where insertion started
    int num_inserted = 0;

    while (1)
    {
        int had_nl = 0;
        char *line = read_full_line(fp, &had_nl);
        if (!line)
            break;

        char **new_lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        if (new_lines == NULL)
        {
            free(line);
            fclose(fp);
            critical_error(ed);
        }
        ed->lines = new_lines;

        // Shift lines down to make room
        for (int i = ed->num_lines; i > insert_pos; i--)
        {
            ed->lines[i] = ed->lines[i - 1];
        }

        ed->lines[insert_pos] = line;
        ed->num_lines++;
        insert_pos++;
        num_inserted++;
        bytes += (int)strlen(line) + (had_nl ? 1 : 0);

        if (!had_nl)
            break; // EOF mid-line
    }

    fclose(fp);
    if (bytes > 0)
    {
        ed->current_line = insert_pos - 1; // 0-indexed: last inserted line
        ed->dirty = 1;
        // Update marks after insertion
        update_marks_after_insert(ed, first_insert_pos, num_inserted);
    }
    PRINTF("%d\n", bytes);
}

// Write append command: append range to existing file
void write_append_file(Editor *ed, AddressRange range, const char *filename)
{
    if (!filename || strlen(filename) == 0)
    {
        set_error(ed, "No filename specified");
        return;
    }

    if (range.start < 0 || range.end < 0 || range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }

    FILE *fp = fopen(filename, "a");
    if (!fp)
    {
        set_error(ed, "Cannot open file for append");
        return;
    }

    int bytes = 0;
    for (int i = range.start; i <= range.end; i++)
    {
        fprintf(fp, "%s\n", ed->lines[i]);
        bytes += strlen(ed->lines[i]) + 1;
    }

    fclose(fp);
    PRINTF("%d\n", bytes);
}

// Change command: delete range and enter insert mode
void change_range(Editor *ed, AddressRange range)
{
    prepare_undo(ed);
    if (range.start < 0 || range.end < 0)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }

    // Delete the range
    delete_range(ed, range);

    // Insert at the position where range started
    int insert_addr = range.start;
    if (insert_addr > ed->num_lines)
        insert_addr = ed->num_lines;
    if (insert_addr < 0)
        insert_addr = 0;

    insert_line(ed, insert_addr);
}

// Move command: move range to after dest_addr
void move_range(Editor *ed, AddressRange range, int dest_addr)
{
    prepare_undo(ed);
    if (range.start < 0 || range.end < 0)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (dest_addr < 0 || dest_addr > ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }

    // Can't move range to within itself
    if (dest_addr >= range.start && dest_addr <= range.end)
    {
        set_error(ed, "Invalid destination");
        return;
    }

    int num_lines = range.end - range.start + 1;

    // Save pointers to lines being moved
    char **moved_lines = malloc(num_lines * sizeof(char *));
    if (!moved_lines)
        critical_error(ed);

    for (int i = 0; i < num_lines; i++)
    {
        moved_lines[i] = ed->lines[range.start + i];
    }

    // Remove lines from source (shift lines after range down)
    for (int i = range.start; i < ed->num_lines - num_lines; i++)
    {
        ed->lines[i] = ed->lines[i + num_lines];
    }

    // Adjust destination if it's after the moved range
    int adjusted_dest = dest_addr;
    if (dest_addr > range.end)
    {
        adjusted_dest -= num_lines;
    }

    // Make room at destination (shift lines after dest up)
    for (int i = ed->num_lines - num_lines - 1; i > adjusted_dest; i--)
    {
        ed->lines[i + num_lines] = ed->lines[i];
    }

    // Insert moved lines at destination
    for (int i = 0; i < num_lines; i++)
    {
        ed->lines[adjusted_dest + 1 + i] = moved_lines[i];
    }

    free(moved_lines);

    // POSIX: Current line should be set to the last line moved
    ed->current_line = adjusted_dest + num_lines; // last moved line (0-indexed)
    ed->dirty = 1;
    
    // Update marks after move operation
    // This is complex: marks in the moved range need to move to the new location
    // Marks between source and dest need to shift
    for (int i = 0; i < 26; i++)
    {
        if (ed->marks[i] < 0)
            continue; // Mark not set
        
        if (ed->marks[i] >= range.start && ed->marks[i] <= range.end)
        {
            // Mark was in moved range - move it to new location
            int offset = ed->marks[i] - range.start; // Offset within moved range
            ed->marks[i] = adjusted_dest + 1 + offset;
        }
        else if (range.start < adjusted_dest + 1)
        {
            // Moving forward: marks between old and new positions shift down
            if (ed->marks[i] > range.end && ed->marks[i] <= adjusted_dest)
            {
                ed->marks[i] -= num_lines;
            }
        }
        else
        {
            // Moving backward: marks between new and old positions shift up
            if (ed->marks[i] >= adjusted_dest + 1 && ed->marks[i] < range.start)
            {
                ed->marks[i] += num_lines;
            }
        }
    }
}

// Copy/transfer command: copy range to after dest_addr
void copy_range(Editor *ed, AddressRange range, int dest_addr)
{
    prepare_undo(ed);
    if (range.start < 0 || range.end < 0)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (dest_addr < 0 || dest_addr > ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }

    int num_lines = range.end - range.start + 1;

    // Allocate space for new lines
    char **new_lines = realloc(ed->lines, (ed->num_lines + num_lines) * sizeof(char *));
    if (!new_lines)
        critical_error(ed);
    ed->lines = new_lines;

    // Make room at destination (shift lines after dest up)
    for (int i = ed->num_lines - 1; i > dest_addr; i--)
    {
        ed->lines[i + num_lines] = ed->lines[i];
    }

    // Copy lines to destination
    for (int i = 0; i < num_lines; i++)
    {
        int src_idx = range.start + i;
        // Adjust source index if it's after the insertion point
        if (src_idx > dest_addr)
            src_idx += num_lines;

        ed->lines[dest_addr + 1 + i] = my_strdup(ed->lines[src_idx]);
        if (!ed->lines[dest_addr + 1 + i])
            critical_error(ed);
    }

    ed->num_lines += num_lines;
    // POSIX: Current line should be set to the last line copied
    ed->current_line = dest_addr + num_lines; // last copied line (0-indexed)
    ed->dirty = 1;
    // Update marks after insertion
    update_marks_after_insert(ed, dest_addr + 1, num_lines);
}

// Join command: join all lines in range into single line
void join_range(Editor *ed, AddressRange range)
{
    prepare_undo(ed);
    if (range.start < 0 || range.end < 0)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }

    // If range is single line, nothing to join
    if (range.start == range.end)
    {
        ed->current_line = range.start; // 0-indexed
        return;
    }

    // Calculate total length needed
    size_t total_len = 0;
    for (int i = range.start; i <= range.end; i++)
    {
        total_len += strlen(ed->lines[i]);
    }

    // Allocate new line
    char *joined = malloc(total_len + 1);
    if (!joined)
        critical_error(ed);
    joined[0] = '\0';

    // Concatenate all lines
    for (int i = range.start; i <= range.end; i++)
    {
        strcat(joined, ed->lines[i]);
        free(ed->lines[i]);
    }

    // Replace first line with joined content
    ed->lines[range.start] = joined;

    // Remove remaining lines in range
    int num_removed = range.end - range.start;
    for (int i = range.start + 1; i < ed->num_lines - num_removed; i++)
    {
        ed->lines[i] = ed->lines[i + num_removed];
    }

    ed->num_lines -= num_removed;

    if (ed->num_lines > 0)
    {
        char **new_lines = realloc(ed->lines, ed->num_lines * sizeof(char *));
        if (!new_lines)
            critical_error(ed);
        ed->lines = new_lines;
    }

    ed->current_line = range.start; // 0-indexed
    ed->dirty = 1;
}

// Substitute over a range using BRE; if global!=0, replace all occurrences per line
void substitute_range(Editor *ed, AddressRange range, const char *pattern, const char *replacement, int global)
{
    prepare_undo(ed);
    if (range.start < 0 || range.end < 0)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (range.start >= ed->num_lines || range.end >= ed->num_lines)
    {
        set_error(ed, "Invalid address");
        return;
    }
    if (!pattern || !replacement)
    {
        set_error(ed, "Invalid substitute");
        return;
    }

    int any_changed = 0;
    for (int j = range.start; j <= range.end; j++)
    {
        BreMatch m;
        if (global)
        {
            char *work = my_strdup(ed->lines[j]);
            if (!work)
                critical_error(ed);
            int changed = 0;
            while (bre_match(work, pattern, &m) == BRE_OK)
            {
                char *next = bre_substitute(work, pattern, replacement);
                if (!next)
                {
                    free(work);
                    critical_error(ed);
                }
                if (strcmp(next, work) == 0)
                {
                    free(next);
                    break;
                }
                free(work);
                work = next;
                changed = 1;
            }
            if (changed)
            {
                free(ed->lines[j]);
                ed->lines[j] = work;
                any_changed = 1;
            }
            else
            {
                free(work);
            }
        }
        else
        {
            if (bre_match(ed->lines[j], pattern, &m) == BRE_OK)
            {
                char *new_line = bre_substitute(ed->lines[j], pattern, replacement);
                if (!new_line)
                    critical_error(ed);
                free(ed->lines[j]);
                ed->lines[j] = new_line;
                any_changed = 1;
            }
        }
    }
    if (!any_changed)
    {
        set_error(ed, "No match");
        return;
    }
    ed->dirty = 1;
    ed->current_line = range.end; // 0-indexed
}

#ifndef LED_TEST
int main(int argc, char *argv[])
{
    Editor ed;
    init_editor(&ed);
    char cmd[MAX_LINE];
    input_fp = stdin;
    int script_mode = 0;

    // Script mode: -S <filename> or --script=<filename>
    for (int ai = 1; ai < argc; ai++)
    {
        const char *arg = argv[ai];
        if (strcmp(arg, "-S") == 0)
        {
            if (ai + 1 >= argc)
            {
                PRINTF("Script file not specified after -S\n");
                return 1;
            }
            const char *fname = argv[++ai];
            FILE *fp = fopen(fname, "r");
            if (!fp)
            {
                PRINTF("Cannot open script file: %s\n", fname);
                return 1;
            }
            input_fp = fp;
            script_mode = 1;
        }
        else if (strncmp(arg, "--script=", 9) == 0)
        {
            const char *fname = arg + 9;
            if (!*fname)
            {
                PRINTF("Script file not specified\n");
                return 1;
            }
            FILE *fp = fopen(fname, "r");
            if (!fp)
            {
                PRINTF("Cannot open script file: %s\n", fname);
                return 1;
            }
            input_fp = fp;
            script_mode = 1;
        }
    }

    // If a filename is provided as a command-line argument (and not in script mode), load it
    if (argc > 1 && !script_mode)
    {
        load_file(&ed, argv[1]);
    }

    if (!script_mode)
    {
        PRINTF("Simple POSIX ed-like editor in C. Type commands (e.g., 'a', 'p', 'q')\n");
    }
    while (1)
    {
        if (fgets(cmd, MAX_LINE, input_fp ? input_fp : stdin) == NULL)
        {
            // EOF or read error: treat as 'Q'
            execute_command(&ed, "Q");
            int is_eof = 0;
            FILE *src = input_fp ? input_fp : stdin;
            if (src)
                is_eof = feof(src);
            if (!is_eof)
            {
                if (input_fp && input_fp != stdin)
                    fclose(input_fp);
                return 1; // non-zero on read error
            }
            if (input_fp && input_fp != stdin)
                fclose(input_fp);
            return 0; // zero on EOF
        }
        execute_command(&ed, cmd);
    }

    free_editor(&ed); // Unreachable due to infinite loop, but good practice
    return 0;
}
#endif
