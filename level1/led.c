#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bre.h"

#define MAX_LINE 1024

typedef struct {
    char **lines;
    int num_lines;
    int current_line;  // 1-based, 0 means none
    int dirty;
} Editor;

static char *my_strdup(const char *s);
static void init_editor(Editor *ed);
static int parse_address(Editor *ed, const char *addr);
static bool parse_range(Editor *ed, const char *cmd, int *start, int *end, char *op);
static void append_line(Editor *ed, int addr);
static void insert_line(Editor *ed, int addr);
static void print_line(Editor *ed, int start, int end);
static void delete_line(Editor *ed, int start, int end);
static void substitute_line(Editor *ed, int start, int end, const char *cmd);
static void write_file(Editor *ed);
static void free_editor(Editor *ed);
static void execute_command(Editor *ed, char *cmd);
static void load_file(Editor *ed, const char *filename);

char *my_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = (char *)malloc(len);
    if (dup == NULL) return NULL;
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

    if (addr[0] == '/' && addr[strlen(addr) - 1] == '/') {
        char pattern[MAX_LINE];
        strncpy(pattern, addr + 1, strlen(addr) - 2);
        pattern[strlen(addr) - 2] = '\0';

        BreMatch match;
        for (int i = ed->current_line - 1; i < ed->num_lines; i++) {
            if (bre_match(ed->lines[i], pattern, &match)) {
                return i;
            }
        }
        for (int i = 0; i < ed->current_line - 1; i++) {
            if (bre_match(ed->lines[i], pattern, &match)) {
                return i;
            }
        }
        return -1; // No match
    }

    int line = atoi(addr);
    if (line <= 0 || line > ed->num_lines) return -1;
    return line - 1;
}

static bool parse_range(Editor *ed, const char *cmd, int *start, int *end, char *op) {
    *start = -1;
    *end = -1;
    *op = '\0';

    char addr1[MAX_LINE] = "";
    char addr2[MAX_LINE] = "";
    const char *p = cmd;
    int i = 0;

    // Extract first address
    while (*p && *p != ',' && i < MAX_LINE - 1) {
        addr1[i++] = *p++;
    }
    addr1[i] = '\0';
    if (*p == ',') p++; else return false; // Must have comma

    // Extract second address
    i = 0;
    while (*p && *p != 'p' && *p != 'd' && *p != 's' && i < MAX_LINE - 1) {
        addr2[i++] = *p++;
    }
    addr2[i] = '\0';
    if (*p) *op = *p; else return false; // Must have command

    *start = parse_address(ed, addr1[0] ? addr1 : "."); // Default to current line
    *end = parse_address(ed, addr2[0] ? addr2 : ".");

    if (*start < 0 || *end < 0 || *start > *end) return false;
    return true;
}

static void append_line(Editor *ed, int addr) {
    char line[MAX_LINE];
    printf("(Enter text, end with '.' on a new line)\n");
    while (fgets(line, MAX_LINE, stdin)) {
        line[strcspn(line, "\n")] = 0;
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
        line[strcspn(line, "\n")] = 0;
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

static void print_line(Editor *ed, int start, int end) {
    if (ed->num_lines == 0 || start < 0 || end >= ed->num_lines || start > end) {
        printf("?\n");
        return;
    }
    for (int i = start; i <= end; i++) {
        printf("%s\n", ed->lines[i]);
    }
    ed->current_line = end + 1;
}

static void delete_line(Editor *ed, int start, int end) {
    if (ed->num_lines == 0 || start < 0 || end >= ed->num_lines || start > end) {
        printf("?\n");
        return;
    }
    for (int i = start; i <= end; i++) {
        free(ed->lines[i]);
    }
    int count = end - start + 1;
    for (int i = start; i < ed->num_lines - count; i++) {
        ed->lines[i] = ed->lines[i + count];
    }
    ed->num_lines -= count;
    ed->lines = realloc(ed->lines, ed->num_lines * sizeof(char *));
    ed->current_line = start + 1 > ed->num_lines ? ed->num_lines : start + 1;
    ed->dirty = 1;
}

static void substitute_line(Editor *ed, int start, int end, const char *cmd) {
    if (ed->num_lines == 0 || start < 0 || end >= ed->num_lines || start > end) {
        printf("?\n");
        return;
    }

    if (cmd[0] != 's' || cmd[1] != '/') {
        printf("?\n");
        return;
    }

    char pattern[MAX_LINE] = {0};
    char replacement[MAX_LINE] = {0};
    const char *p = cmd + 2;
    int i = 0;

    while (*p && *p != '/' && i < MAX_LINE - 1) {
        pattern[i++] = *p++;
    }
    if (*p != '/') {
        printf("?\n");
        return;
    }
    pattern[i] = '\0';
    p++;

    i = 0;
    while (*p && *p != '/' && i < MAX_LINE - 1) {
        replacement[i++] = *p++;
    }
    if (*p != '/') {
        printf("?\n");
        return;
    }
    replacement[i] = '\0';

    for (int j = start; j <= end; j++) {
        char *new_line = bre_substitute(ed->lines[j], pattern, replacement);
        if (!new_line) {
            printf("?\n");
            return;
        }
        free(ed->lines[j]);
        ed->lines[j] = new_line;
    }
    ed->dirty = 1;
    ed->current_line = end + 1;
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
        bytes += strlen(ed->lines[i]) + 1;
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
    cmd[strcspn(cmd, "\n")] = 0;
    if (strlen(cmd) == 0) return;

    // Check for range-based command
    int start, end;
    char op;
    if (parse_range(ed, cmd, &start, &end, &op)) {
        switch (op) {
            case 'p': print_line(ed, start, end); break;
            case 'd': delete_line(ed, start, end); break;
            case 's': substitute_line(ed, start, end, cmd); break;
            default: printf("?\n");
        }
        return;
    }

    // Single pattern or substitution
    if (cmd[0] == '/' || cmd[0] == 's') {
        if (cmd[0] == 's') {
            int addr = ed->current_line - 1;
            substitute_line(ed, addr, addr, cmd);
        } else {
            int addr = parse_address(ed, cmd);
            if (addr >= 0 && addr < ed->num_lines) {
                ed->current_line = addr + 1;
                print_line(ed, addr, addr);
            } else {
                printf("?\n");
            }
        }
        return;
    }

    char addr_str[MAX_LINE] = "";
    char op_single = cmd[strlen(cmd) - 1];
    if (strlen(cmd) > 1) {
        strncpy(addr_str, cmd, strlen(cmd) - 1);
        addr_str[strlen(cmd) - 1] = 0;
    } else {
        op_single = cmd[0];
    }

    if (strspn(cmd, "0123456789.$") == strlen(cmd)) {
        int addr = parse_address(ed, cmd);
        if (addr >= 0 && addr < ed->num_lines) ed->current_line = addr + 1;
        return;
    }

    int addr = parse_address(ed, addr_str);
    if (addr < 0 && op_single != 'a' && op_single != 'q' && op_single != 'w') {
        printf("?\n");
        return;
    }

    switch (op_single) {
        case 'a': append_line(ed, addr < 0 ? ed->num_lines - 1 : addr); break;
        case 'i': insert_line(ed, addr < 0 ? 0 : addr); break;
        case 'p': print_line(ed, addr, addr); break;
        case 'd': delete_line(ed, addr, addr); break;
        case 'w': write_file(ed); break;
        case 'q': if (ed->dirty) printf("?\n"); else exit(0); break;
        default: printf("?\n");
    }
}

static void load_file(Editor *ed, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("?\n");
        return;
    }

    char line[MAX_LINE];
    int bytes = 0;
    while (fgets(line, MAX_LINE, fp)) {
        line[strcspn(line, "\n")] = 0;
        ed->lines = realloc(ed->lines, (ed->num_lines + 1) * sizeof(char *));
        ed->lines[ed->num_lines] = my_strdup(line);
        if (ed->lines[ed->num_lines] == NULL) {
            printf("?\n");
            fclose(fp);
            return;
        }
        ed->num_lines++;
        bytes += strlen(line) + 1;
    }
    fclose(fp);
    ed->current_line = ed->num_lines;
    printf("%d\n", bytes);
}

int main(int argc, char *argv[]) {
    Editor ed;
    init_editor(&ed);
    char cmd[MAX_LINE];

    if (argc > 1) {
        load_file(&ed, argv[1]);
    }

    printf("Simple POSIX ed-like editor in C. Type commands (e.g., 'a', '/pattern/', '1,/foo/p', 's/pattern/repl/', 'q')\n");
    while (1) {
        fgets(cmd, MAX_LINE, stdin);
        execute_command(&ed, cmd);
    }

    free_editor(&ed);
    return 0;
}
