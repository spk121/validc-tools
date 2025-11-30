#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void print_help(void) {
    printf("Usage: mv source dest\n");
    printf("Move (rename) a file from source to destination.\n");
    printf("Options:\n");
    printf("  -h, --help  Display this help message\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return (argc < 2) ? 1 : 0;
    }

    if (argc != 3) {
        fprintf(stderr, "Error: Expected exactly 2 arguments (source and dest)\n");
        print_help();
        return 1;
    }

    const char *source = argv[1];
    const char *dest = argv[2];

    if (rename(source, dest) != 0) {
        fprintf(stderr, "Error: Cannot move '%s' to '%s'\n", source, dest);
        return 1;
    }

    return 0;
}
