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
    printf("Usage: fnvupdate [options]\n");
    printf("Update file_hash.dat by recomputing FNV-1a hashes for listed files.\n");
    printf("Options:\n");
    printf("  -h, --help  Display this help message\n");
    printf("Updates entries if hashes change, removes entries for missing files.\n");
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
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else {
            fprintf(stderr, "Error: Unexpected argument '%s'\n", argv[i]);
            print_help();
            return 1;
        }
    }

    FILE *db = fopen("file_hash.dat", "r");
    if (!db) {
        fprintf(stderr, "Error: Cannot open file_hash.dat (may not exist)\n");
        return 1;
    }

    FILE *temp = tmpfile();
    if (!temp) {
        fprintf(stderr, "Error: Cannot create temporary file\n");
        fclose(db);
        return 1;
    }

    char line[1024];
    char stored_filename[512], stored_hash[9], stored_time[20];
    bool modified = false;

    while (fgets(line, sizeof(line), db)) {
        if (sscanf(line, "\"%511[^\"]\" %8s %19s", stored_filename, stored_hash, stored_time) == 3) {
            uint32_t current_hash = fnv1a_hash_file(stored_filename);
            if (current_hash == 0) {
                // File missing, skip this entry
                modified = true;
                continue;
            }
            char current_hash_str[9];
            sprintf(current_hash_str, "%08x", current_hash);
            if (strcmp(stored_hash, current_hash_str) == 0) {
                // Hash unchanged, keep original line
                fputs(line, temp);
            } else {
                // Hash changed, update timestamp
                char new_time[20];
                get_iso_time(new_time);
                fprintf(temp, "\"%s\" %s %s\n", stored_filename, current_hash_str, new_time);
                modified = true;
            }
        } else {
            // Malformed line, preserve it (could log a warning)
            fputs(line, temp);
        }
    }

    fclose(db);

    if (modified) {
        db = fopen("file_hash.dat", "w");
        if (!db) {
            fprintf(stderr, "Error: Cannot rewrite file_hash.dat\n");
            fclose(temp);
            return 1;
        }
        rewind(temp);
        while (fgets(line, sizeof(line), temp)) {
            fputs(line, db);
        }
        fclose(db);
    }

    fclose(temp);
    return 0;
}
