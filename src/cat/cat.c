#include <stdio.h>
#include <string.h>
#include <stdbool.h>

void print_help(void) {
    printf("Usage: cat [options] [file ...]\n");
    printf("Concatenate and print files to stdout.\n");
    printf("Options:\n");
    printf("  -h, --help  Display this help message\n");
    printf("If no files are specified, reads from stdin.\n");
}

bool cat_file(FILE *fp, const char *filename) {
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (putchar(c) == EOF) {
            fprintf(stderr, "Error: Failed to write to stdout\n");
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    bool success = true;

    // Process command-line options
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else {
            break; // First non-option is a file
        }
    }

    // No files specified, read from stdin
    if (i >= argc) {
        if (!cat_file(stdin, "stdin")) {
            return 1;
        }
        return 0;
    }

    // Process each file
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            // Special case: "-" means stdin
            if (!cat_file(stdin, "stdin")) {
                success = false;
            }
        } else {
            FILE *fp = fopen(argv[i], "r");
            if (fp == NULL) {
                fprintf(stderr, "Error: Cannot open file '%s'\n", argv[i]);
                success = false;
                continue;
            }
            if (!cat_file(fp, argv[i])) {
                success = false;
            }
            fclose(fp);
        }
    }

    return success ? 0 : 1;
}
