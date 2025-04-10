#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void print_help(void) {
    printf("Usage: rm file\n");
    printf("Remove a file.\n");
    printf("Options:\n");
    printf("  -h, --help  Display this help message\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return (argc < 2) ? 1 : 0;
    }

    if (argc != 2) {
        fprintf(stderr, "Error: Expected exactly 1 argument (file to remove)\n");
        print_help();
        return 1;
    }

    const char *file = argv[1];
    if (remove(file) != 0) {
        fprintf(stderr, "Error: Cannot remove file '%s'\n", file);
        return 1;
    }

    return 0;
}
