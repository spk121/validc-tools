#ifndef LED_LED_H
#define LED_LED_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LINE 1024

typedef struct {
    int start;  // 0-based start line, -1 if invalid
    int end;    // 0-based end line, -1 if invalid
} AddressRange;

typedef struct {
    char **lines;
    int num_lines;
    int current_line;
    int dirty;
    char *filename;  // Current filename, or NULL if none
    int verbose;      // Verbose (help) mode flag
    char *last_error; // Last error context string (heap allocated)
    int marks[26];    // 'a'..'z' marks -> 0-based line index, -1 if unset
    int prompt;       // prompt toggle (unused in tests)
    // Single-level undo snapshot
    char **undo_lines;
    int undo_num_lines;
    int undo_current_line;
    int undo_valid;   // 1 if undo snapshot valid
} Editor;

void init_editor(Editor *ed);
void free_editor(Editor *ed);

// Expose for testing
int parse_address(Editor *ed, const char *addr);
AddressRange parse_address_range(Editor *ed, const char *range_str);

// New address parsing functions from addrparse.c
#define ADDR_NONE  (-1)
#define ADDR_ERROR (-2)
int parse_one_address(const char **pp, int current, int last_line, const int marks[26]);
const char *parse_ed_address(const char *line, int *addr1, int *addr2, bool *have_comma, int current_line, int last_line, const int marks[26]);
void append_line(Editor *ed, int addr);
void insert_line(Editor *ed, int addr);
void print_line(Editor *ed, int addr);
void print_range(Editor *ed, AddressRange range);
void print_numbered_range(Editor *ed, AddressRange range);
void print_list_range(Editor *ed, AddressRange range);
void delete_line(Editor *ed, int addr);
void delete_range(Editor *ed, AddressRange range);
void load_file(Editor *ed, const char *filename);
void write_file(Editor *ed, const char *filename);
void edit_file(Editor *ed, const char *filename);
void forced_edit_file(Editor *ed, const char *filename);
void read_file_at_address(Editor *ed, int addr, const char *filename);
void write_append_file(Editor *ed, AddressRange range, const char *filename);
void change_range(Editor *ed, AddressRange range);
void move_range(Editor *ed, AddressRange range, int dest_addr);
void copy_range(Editor *ed, AddressRange range, int dest_addr);
void join_range(Editor *ed, AddressRange range);
// Substitute command over a range; if global!=0, replace all occurrences per line
void substitute_range(Editor *ed, AddressRange range, const char *pattern, const char *replacement, int global);
// Verbose/error helpers
void set_verbose(Editor *ed, int on);
const char *get_last_error(Editor *ed);
void clear_last_error(Editor *ed);

#ifdef __cplusplus
}
#endif

#endif // LED_LED_H
