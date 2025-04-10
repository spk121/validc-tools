#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 1024  // Maximum length of a line

typedef struct {
    char **lines;      // Array of lines
    int num_lines;     // Number of lines in buffer
    int current_line;  // Current line (1-based, 0 means none)
    int dirty;         // Tracks unsaved changes
} Editor;

static char *my_strdup(const char *s);
static void init_editor(Editor *ed);
static int parse_address(Editor *ed, const char *addr);
static void append_line(Editor *ed, int addr);
static void insert_line(Editor *ed, int addr);
static void print_line(Editor *ed, int addr);
static void delete_line(Editor *ed, int addr);
static void write_file(Editor *ed);
static void free_editor(Editor *ed);
static void execute_command(Editor *ed, char *cmd);
static void load_file(Editor *ed, const char *filename);  // New function

char *my_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;  // Include null terminator
    char *dup = (char *)malloc(len);
    if (dup == NULL) return NULL;  // Allocation failed
    strcpy(dup, s);
    return dup;
}

static void init_editor(Editor *ed) {
    ed->lines = NULL;
    ed->num_lines = 0;
    ed->current_line = 0;
    ed->dirty = 0;
}

static int parse_address(Editor *ed, const char *addr) {
    if (!addr || strlen(addr) == 0) return ed->current_line - 1;
    if (strcmp(addr, ".") == 0) return ed->current_line - 1;
    if (strcmp(addr, "$") == 0) return ed->num_lines - 1;
    int line = atoi(addr);
    if (line <= 0 || line > ed->num_lines) return -1;  // Invalid
    return line - 1;  // Convert to 0-based
}

static void append_line(Editor *ed, int addr) {
    char line[MAX_LINE];
    printf("(Enter text, end with '.' on a new line)\n");
    while (fgets(line, MAX_LINE, stdin)) {
        line[strcspn(line, "\n")] = 0;  // Remove newline
        if (strcmp(line, ".") == 0) break;

        ed->lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        ed->lines[ed->num_lines] = my_strdup(line);
        ed->num_lines++;
        if (addr < ed->num_lines - 1) {
            for (int i = ed->num_lines - 1; i > addr + 1; i--) {
                char *temp = ed->lines[i];
                ed->lines[i] = ed->lines[i - 1];
                ed->lines[i - 1] = temp;
            }
            addr++;
        }
    }
    ed->current_line = addr + 1;
    ed->dirty = 1;
}

static void insert_line(Editor *ed, int addr) {
    char line[MAX_LINE];
    printf("(Enter text, end with '.' on a new line)\n");
    while (fgets(line, MAX_LINE, stdin)) {
        line[strcspn(line, "\n")] = 0;  // Remove newline
        if (strcmp(line, ".") == 0) break;

        ed->lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        for (int i = ed->num_lines; i > addr; i--) {
            ed->lines[i] = ed->lines[i - 1];
        }
        ed->lines[addr] = my_strdup(line);
        ed->num_lines++;
        addr++;
    }
    ed->current_line = addr;
    ed->dirty = 1;
}

static void print_line(Editor *ed, int addr) {
    if (ed->num_lines == 0 || addr < 0 || addr >= ed->num_lines) {
        printf("?\n");
        return;
    }
    printf("%s\n", ed->lines[addr]);
    ed->current_line = addr + 1;
}

static void delete_line(Editor *ed, int addr) {
    if (ed->num_lines == 0 || addr < 0 || addr >= ed->num_lines) {
        printf("?\n");
        return;
    }
    free(ed->lines[addr]);
    for (int i = addr; i < ed->num_lines - 1; i++) {
        ed->lines[i] = ed->lines[i + 1];
    }
    ed->num_lines--;
    ed->lines = realloc(ed->lines, ed->num_lines * sizeof(char *));
    ed->current_line = addr + 1 > ed->num_lines ? ed->num_lines : addr + 1;
    ed->dirty = 1;
}

static void write_file(Editor *ed) {
    char filename[MAX_LINE];
    printf("Enter filename: ");
    fgets(filename, MAX_LINE, stdin);
    filename[strcspn(filename, "\n")] = 0;

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        printf("?\n");
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
}

static void free_editor(Editor *ed) {
    for (int i = 0; i < ed->num_lines; i++) {
        free(ed->lines[i]);
    }
    free(ed->lines);
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
        if (addr >= 0 && addr < ed->num_lines) ed->current_line = addr + 1;
        return;
    }

    int addr = parse_address(ed, addr_str);
    if (addr < 0 && op != 'a' && op != 'q' && op != 'w') {
        printf("?\n");
        return;
    }

    switch (op) {
        case 'a': append_line(ed, addr < 0 ? ed->num_lines - 1 : addr); break;
        case 'i': insert_line(ed, addr < 0 ? 0 : addr); break;
        case 'p': print_line(ed, addr); break;
        case 'd': delete_line(ed, addr); break;
        case 'w': write_file(ed); break;
        case 'q': if (ed->dirty) printf("?\n"); else exit(0); break;
        default: printf("?\n");
    }
}

// New function to load a file into the editor buffer
static void load_file(Editor *ed, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("?\n");  // File not found or can't be opened
        return;
    }

    char line[MAX_LINE];
    int bytes = 0;
    while (fgets(line, MAX_LINE, fp)) {
        line[strcspn(line, "\n")] = 0;  // Remove newline
        ed->lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        ed->lines[ed->num_lines] = my_strdup(line);
        if (ed->lines[ed->num_lines] == NULL) {
            printf("?\n");  // Memory allocation failed
            fclose(fp);
            return;
        }
        ed->num_lines++;
        bytes += strlen(line) + 1;  // Count bytes including newline
    }
    fclose(fp);
    ed->current_line = ed->num_lines;  // Set current line to last line
    printf("%d\n", bytes);  // Print byte count like POSIX ed
}

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
