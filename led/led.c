#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "led.h"

static char *my_strdup(const char *s);
void init_editor(Editor *ed);
int parse_address(Editor *ed, const char *addr);
void append_line(Editor *ed, int addr);
void insert_line(Editor *ed, int addr);
void print_line(Editor *ed, int addr);
void delete_line(Editor *ed, int addr);
void write_file(Editor *ed, const char *filename);
void free_editor(Editor *ed);
static void execute_command(Editor *ed, char *cmd);
void load_file(Editor *ed, const char *filename);
static char *read_full_line(FILE *fp, int *had_newline);
static void critical_error(Editor *ed); // forward declaration for set_error
static void set_error(Editor *ed, const char *msg);
void set_verbose(Editor *ed, int on) { ed->verbose = on ? 1 : 0; }
const char *get_last_error(Editor *ed) { return ed->last_error; }
void clear_last_error(Editor *ed) { if (ed->last_error) { free(ed->last_error); ed->last_error = NULL; } }

static void set_error(Editor *ed, const char *msg) {
    if (!msg) msg = "Unknown error";
    // Store last error (duplicate)
    if (ed->last_error) free(ed->last_error);
    ed->last_error = my_strdup(msg);
    if (!ed->last_error) {
        // Allocation failure while storing error: escalate
        critical_error(ed);
    }
    if (ed->verbose) {
        printf("%s\n", msg);
    } else {
        printf("?\n");
    }
}

char *my_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;  // Include null terminator
    char *dup = (char *)malloc(len);
    if (dup == NULL) return NULL;  // Allocation failed
    strcpy(dup, s);
    return dup;
}

// Read an entire line from fp, handling arbitrarily long input.
// Returns a heap-allocated string WITHOUT the trailing newline.
// Sets *had_newline to 1 if a newline was consumed, 0 if EOF ended the line.
// Returns NULL if EOF encountered before any characters were read.
static char *read_full_line(FILE *fp, int *had_newline) {
    if (had_newline) *had_newline = 0;
    size_t cap = 128;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') { if (had_newline) *had_newline = 1; break; }
        if (c == '\r') { // Handle CRLF: peek next
            int next = fgetc(fp);
            if (next == '\n') { if (had_newline) *had_newline = 1; break; }
            // Not CRLF, push back next
            if (next != EOF) ungetc(next, fp);
            // Treat '\r' as normal char
        }
        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            char *new_buf = (char *)realloc(buf, new_cap);
            if (!new_buf) { free(buf); return NULL; }
            buf = new_buf;
            cap = new_cap;
        }
        buf[len++] = (char)c;
    }
    if (c == EOF && len == 0) { free(buf); return NULL; }
    buf[len] = '\0';
    return buf;
}

static void emergency_save(Editor *ed) {
    char filename[MAX_LINE];
    char *target = ed->filename;
    
    if (target == NULL) {
        printf("Enter filename to save: ");
        if (fgets(filename, MAX_LINE, stdin) == NULL) {
            printf("Save failed.\n");
            return;
        }
        filename[strcspn(filename, "\n")] = 0;
        target = filename;
    }
    
    FILE *fp = fopen(target, "w");
    if (!fp) {
        printf("Could not open %s for writing.\n", target);
        if (ed->filename == NULL) {
            printf("Enter alternate filename: ");
            if (fgets(filename, MAX_LINE, stdin) == NULL) {
                printf("Save failed.\n");
                return;
            }
            filename[strcspn(filename, "\n")] = 0;
            fp = fopen(filename, "w");
            if (!fp) {
                printf("Could not save buffer.\n");
                return;
            }
        } else {
            return;
        }
    }
    
    int bytes = 0;
    for (int i = 0; i < ed->num_lines; i++) {
        fprintf(fp, "%s\n", ed->lines[i]);
        bytes += strlen(ed->lines[i]) + 1;
    }
    fclose(fp);
    printf("Saved %d bytes.\n", bytes);
}

static void critical_error(Editor *ed) {
    printf("\n*** CRITICAL ERROR: Memory allocation failed ***\n");
    printf("The program must exit. Save current buffer? (Y/N): ");
    char response[MAX_LINE];
    if (fgets(response, MAX_LINE, stdin) == NULL) {
        exit(1);
    }
    response[strcspn(response, "\n")] = 0;
    if (response[0] == 'Y' || response[0] == 'y') {
        emergency_save(ed);
    }
    exit(1);
}

void init_editor(Editor *ed) {
    ed->lines = NULL;
    ed->num_lines = 0;
    ed->current_line = 0;
    ed->dirty = 0;
    ed->filename = NULL;
    ed->verbose = 0;
    ed->last_error = NULL;
}

int parse_address(Editor *ed, const char *addr) {
    if (!addr || strlen(addr) == 0) {
        // Empty address: use current line, or 0 if buffer is empty
        return (ed->num_lines > 0) ? ed->current_line - 1 : 0;
    }
    if (strcmp(addr, ".") == 0) {
        // Current line, or 0 if buffer is empty
        return (ed->num_lines > 0) ? ed->current_line - 1 : 0;
    }
    if (strcmp(addr, "$") == 0) {
        // Last line (will be -1 if buffer is empty, which is correct)
        return ed->num_lines - 1;
    }
    int line = atoi(addr);
    // Line 0 is invalid in ed (lines are 1-based)
    // But we return -1 to signal invalid address
    if (line <= 0 || line > ed->num_lines) return -1;
    return line - 1;  // Convert to 0-based
}

void append_line(Editor *ed, int addr) {
    int original_num_lines = ed->num_lines;
    int original_current = ed->current_line;
    int last_inserted_index = -1;
    printf("(Enter text, end with '.' on a new line)\n");
    while (1) {
        int had_nl = 0;
        char *line = read_full_line(stdin, &had_nl);
        if (!line) break; // EOF
        if (strcmp(line, ".") == 0) { free(line); break; }
        // Insert after addr: result position is addr+1
        char **new_lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        if (new_lines == NULL) { free(line); critical_error(ed); }
        ed->lines = new_lines;
        for (int i = ed->num_lines; i > addr + 1; i--) {
            ed->lines[i] = ed->lines[i - 1];
        }
        ed->lines[addr + 1] = line; // already allocated
        ed->num_lines++;
        addr++;
        last_inserted_index = addr;
        if (!had_nl) break; // Last line without newline (EOF mid-line)
    }
    if (last_inserted_index >= 0) {
        ed->current_line = last_inserted_index + 1; // 1-based
    } else {
        ed->current_line = original_current; // unchanged if nothing appended
    }
    if (ed->num_lines > original_num_lines) ed->dirty = 1;
}

void insert_line(Editor *ed, int addr) {
    int original_num_lines = ed->num_lines;
    int original_current = ed->current_line;
    int last_inserted_index = -1;
    printf("(Enter text, end with '.' on a new line)\n");
    while (1) {
        int had_nl = 0;
        char *line = read_full_line(stdin, &had_nl);
        if (!line) break; // EOF
        if (strcmp(line, ".") == 0) { free(line); break; }
        char **new_lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        if (new_lines == NULL) { free(line); critical_error(ed); }
        ed->lines = new_lines;
        for (int i = ed->num_lines; i > addr; i--) {
            ed->lines[i] = ed->lines[i - 1];
        }
        ed->lines[addr] = line;
        ed->num_lines++;
        last_inserted_index = addr;
        addr++;
        if (!had_nl) break;
    }
    if (last_inserted_index >= 0) {
        ed->current_line = last_inserted_index + 1; // 1-based of last inserted
    } else {
        ed->current_line = original_current; // unchanged if nothing inserted
    }
    if (ed->num_lines > original_num_lines) ed->dirty = 1;
}

void print_line(Editor *ed, int addr) {
    if (ed->num_lines == 0 || addr < 0 || addr >= ed->num_lines) {
        set_error(ed, "Invalid address");
        return;
    }
    printf("%s\n", ed->lines[addr]);
    ed->current_line = addr + 1;
}

void delete_line(Editor *ed, int addr) {
    if (ed->num_lines == 0 || addr < 0 || addr >= ed->num_lines) {
        set_error(ed, "Invalid address");
        return;
    }
    free(ed->lines[addr]);
    for (int i = addr; i < ed->num_lines - 1; i++) {
        ed->lines[i] = ed->lines[i + 1];
    }
    ed->num_lines--;
    if (ed->num_lines == 0) {
        free(ed->lines);
        ed->lines = NULL;
    } else {
        char **new_lines = realloc(ed->lines, ed->num_lines * sizeof(char *));
        if (new_lines == NULL) {
            critical_error(ed);
        }
        ed->lines = new_lines;
    }
    ed->current_line = addr + 1 > ed->num_lines ? ed->num_lines : addr + 1;
    // Line was successfully deleted, so set dirty flag
    ed->dirty = 1;
}

void write_file(Editor *ed, const char *filename) {
    const char *target = filename;
    if (target == NULL || strlen(target) == 0) {
        target = ed->filename;
    }
    if (target == NULL || strlen(target) == 0) {
        set_error(ed, "No current filename");
        return;
    }

    FILE *fp = fopen(target, "w");
    if (!fp) {
        set_error(ed, "Write failed");
        return;
    }
    int bytes = 0;
    for (int i = 0; i < ed->num_lines; i++) {
        fprintf(fp, "%s\n", ed->lines[i]);
        bytes += strlen(ed->lines[i]) + 1;  // Include newline
    }
    fclose(fp);
    printf("%d\n", bytes);
    ed->dirty = 0;
    
    // Update filename if a new one was specified
    if (filename != NULL && strlen(filename) > 0) {
        if (ed->filename) {
            free(ed->filename);
        }
        ed->filename = my_strdup(filename);
        if (ed->filename == NULL) {
            critical_error(ed);
        }
    }
}

void free_editor(Editor *ed) {
    if (!ed) return;
    for (int i = 0; i < ed->num_lines; i++) {
        free(ed->lines[i]);
    }
    free(ed->lines);
    ed->lines = NULL;
    free(ed->filename);
    ed->filename = NULL;
    if (ed->last_error) free(ed->last_error);
    ed->last_error = NULL;
    ed->num_lines = 0;
    ed->current_line = 0;
    ed->dirty = 0;
    ed->verbose = 0;
}

static void execute_command(Editor *ed, char *cmd) {
    cmd[strcspn(cmd, "\n")] = 0;  // Remove newline
    if (strlen(cmd) == 0) return;

    char addr_str[MAX_LINE] = "";
    char op = cmd[strlen(cmd) - 1];
    if (strlen(cmd) > 1) {
        strncpy(addr_str, cmd, strlen(cmd) - 1);
        addr_str[strlen(cmd) - 1] = 0;
    } else {
        op = cmd[0];
    }

    if (strspn(cmd, "0123456789.$") == strlen(cmd)) {
        int addr = parse_address(ed, cmd);
        if (addr >= 0 && addr < ed->num_lines) {
            ed->current_line = addr + 1;
        } else {
            set_error(ed, "Invalid address");
        }
        return;
    }

    // Special handling for 'w' command which may have a filename
    if (op == 'w') {
        // Check if there's a space followed by a filename
        char *space = strchr(cmd, ' ');
        if (space != NULL) {
            // Skip the space and get the filename
            char *filename = space + 1;
            while (*filename == ' ') filename++;  // Skip multiple spaces
            if (strlen(filename) > 0) {
                write_file(ed, filename);
                return;
            }
        }
        write_file(ed, NULL);  // No filename provided
        return;
    }

    int addr = parse_address(ed, addr_str);
    if (addr < 0 && op != 'a' && op != 'q') {
        set_error(ed, "Invalid address");
        return;
    }

    switch (op) {
        case 'a': append_line(ed, addr < 0 ? ed->num_lines - 1 : addr); break;
        case 'i': insert_line(ed, addr < 0 ? 0 : addr); break;
        case 'p': print_line(ed, addr); break;
        case 'd': delete_line(ed, addr); break;
        case 'q': if (ed->dirty) set_error(ed, "Buffer modified"); else exit(0); break;
        case 'H': ed->verbose = !ed->verbose; printf("Verbose %s\n", ed->verbose ? "on" : "off"); break;
        case 'h': if (ed->last_error) printf("%s\n", ed->last_error); else printf("No error\n"); break;
        default: set_error(ed, "Unknown command");
    }
}

// New function to load a file into the editor buffer
void load_file(Editor *ed, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { set_error(ed, "Cannot open file"); return; }
    int bytes = 0;
    while (1) {
        int had_nl = 0;
        char *line = read_full_line(fp, &had_nl);
        if (!line) break;
        char **new_lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        if (new_lines == NULL) { free(line); fclose(fp); critical_error(ed); }
        ed->lines = new_lines;
        ed->lines[ed->num_lines] = line;
        ed->num_lines++;
        bytes += (int)strlen(line) + (had_nl ? 1 : 0);
        if (!had_nl) break; // EOF mid-line
    }
    fclose(fp);
    ed->current_line = ed->num_lines;
    printf("%d\n", bytes);
    if (ed->filename) free(ed->filename);
    ed->filename = my_strdup(filename);
    if (ed->filename == NULL) critical_error(ed);
}

#ifndef LED_TEST
int main(int argc, char *argv[]) {
    Editor ed;
    init_editor(&ed);
    char cmd[MAX_LINE];

    // If a filename is provided as a command-line argument, load it
    if (argc > 1) {
        load_file(&ed, argv[1]);
    }

    printf("Simple POSIX ed-like editor in C. Type commands (e.g., 'a', 'p', 'q')\n");
    while (1) {
        fgets(cmd, MAX_LINE, stdin);
        execute_command(&ed, cmd);
    }

    free_editor(&ed);  // Unreachable due to infinite loop, but good practice
    return 0;
}
#endif
