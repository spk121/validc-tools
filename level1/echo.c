#include <stdio.h>
#include <string.h>
#include <stdbool.h>

void print_help(void) {
    printf("Usage: echo [options] [text ...]\n");
    printf("Print text to stdout.\n");
    printf("Options:\n");
    printf("  -h, --help  Display this help message\n");
    printf("  -n          Do not append a newline\n");
    printf("If no text is provided, prints a single newline.\n");
}

int main(int argc, char *argv[]) {
    bool newline = true;

    // Process command-line options
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[i], "-n") == 0) {
            newline = false;
        } else {
            // First non-option argument, break to start printing
            break;
        }
    }

    // Print arguments
    if (i >= argc) {
        // No arguments after options, just print newline (or nothing if -n)
        if (newline) {
            printf("\n");
        }
    } else {
        // Print all remaining arguments with spaces between them
        for (; i < argc; i++) {
            printf("%s", argv[i]);
            if (i < argc - 1) {
                printf(" ");
            }
        }
        if (newline) {
            printf("\n");
        }
    }

    return 0;
}