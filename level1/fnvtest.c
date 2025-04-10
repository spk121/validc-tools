#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

uint32_t fnv1a_hash_file(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        return 0; // Error indicator
    }

    uint32_t hash = 2166136261U;
    const uint32_t prime = 16777619U;
    int c;

    while ((c = fgetc(fp)) != EOF) {
        hash ^= (uint8_t)c;
        hash *= prime;
    }

    fclose(fp);
    return hash;
}

void print_help(void) {
    printf("Usage: fnvtest [options] filename\n");
    printf("Compute FNV-1a hash of a file and compare with file_hash.dat.\n");
    printf("Options:\n");
    printf("  -h, --help  Display this help message\n");
    printf("Returns 0 if hash matches stored value, 1 if changed or new (updates file_hash.dat).\n");
}

void get_iso_time(char time_str[20]) {
    time_t now;
    time(&now);
    struct tm *tm = localtime(&now);
    sprintf(time_str, "%04d-%02d-%02dT%02d:%02d:%02d",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: No filename provided\n");
        print_help();
        return 1;
    }

    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else if (filename == NULL) {
            filename = argv[i];
        } else {
            fprintf(stderr, "Error: Too many arguments\n");
            print_help();
            return 1;
        }
    }

    uint32_t current_hash = fnv1a_hash_file(filename);
    if (current_hash == 0) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return 1;
    }

    char current_hash_str[9];
    sprintf(current_hash_str, "%08x", current_hash);

    FILE *db = fopen("file_hash.dat", "r+");
    if (!db) {
        db = fopen("file_hash.dat", "w");
        if (!db) {
            fprintf(stderr, "Error: Cannot create file_hash.dat\n");
            return 1;
        }
    }

    char line[1024];
    char stored_filename[512], stored_hash[9], stored_time[20];
    bool found = false;
    FILE *temp = tmpfile();
    if (!temp) {
        fprintf(stderr, "Error: Cannot create temporary file\n");
        fclose(db);
        return 1;
    }

    while (fgets(line, sizeof(line), db)) {
        if (sscanf(line, "\"%511[^\"]\" %8s %19s", stored_filename, stored_hash, stored_time) == 3) {
            if (strcmp(stored_filename, filename) == 0) {
                found = true;
                if (strcmp(stored_hash, current_hash_str) == 0) {
                    fputs(line, temp);
                    while (fgets(line, sizeof(line), db)) {
                        fputs(line, temp);
                    }
                    rewind(temp);
                    rewind(db);
                    while (fgets(line, sizeof(line), temp)) {
                        fputs(line, db);
                    }
                    fclose(temp);
                    fclose(db);
                    return 0; // Unchanged
                }
                continue; // Skip old entry
            }
        }
        fputs(line, temp);
    }

    char new_time[20];
    get_iso_time(new_time);
    fprintf(temp, "\"%s\" %s %s\n", filename, current_hash_str, new_time);

    rewind(temp);
    rewind(db);
    ftruncate(fileno(db), 0);
    while (fgets(line, sizeof(line), temp)) {
        fputs(line, db);
    }

    fclose(temp);
    fclose(db);
    return 1; // Changed or new
}