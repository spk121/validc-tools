#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void print_help(void) {
    printf("Usage: cp source dest\n");
    printf("Copy a file from source to destination.\n");
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

    FILE *src_file = fopen(source, "rb");
    if (!src_file) {
        fprintf(stderr, "Error: Cannot open source file '%s'\n", source);
        return 1;
    }

    FILE *dest_file = fopen(dest, "wb");
    if (!dest_file) {
        fprintf(stderr, "Error: Cannot open destination file '%s'\n", dest);
        fclose(src_file);
        return 1;
    }

    int c;
    while ((c = fgetc(src_file)) != EOF) {
        if (fputc(c, dest_file) == EOF) {
            fprintf(stderr, "Error: Failed to write to '%s'\n", dest);
            fclose(src_file);
            fclose(dest_file);
            return 1;
        }
    }

    fclose(src_file);
    fclose(dest_file);
    return 0;
}