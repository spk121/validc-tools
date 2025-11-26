#ifndef LED_LED_H
#define LED_LED_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LINE 1024

typedef struct {
    char **lines;
    int num_lines;
    int current_line;
    int dirty;
    char *filename;  // Current filename, or NULL if none
    int verbose;      // Verbose (help) mode flag
    char *last_error; // Last error context string (heap allocated)
} Editor;

void init_editor(Editor *ed);
void free_editor(Editor *ed);

// Expose for testing
int parse_address(Editor *ed, const char *addr);
void append_line(Editor *ed, int addr);
void insert_line(Editor *ed, int addr);
void print_line(Editor *ed, int addr);
void delete_line(Editor *ed, int addr);
void load_file(Editor *ed, const char *filename);
void write_file(Editor *ed, const char *filename);
// Verbose/error helpers
void set_verbose(Editor *ed, int on);
const char *get_last_error(Editor *ed);
void clear_last_error(Editor *ed);

#ifdef __cplusplus
}
#endif

#endif // LED_LED_H
