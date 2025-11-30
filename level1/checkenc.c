#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static bool is_valid_utf8(const unsigned char *b, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = b[i];
        if (c < 0x80) { i++; continue; } // ASCII
        size_t need = 0;
        if ((c & 0xE0) == 0xC0) { need = 1; if (c < 0xC2) return false; } // avoid overlong
        else if ((c & 0xF0) == 0xE0) { need = 2; }
        else if ((c & 0xF8) == 0xF0) { need = 3; if (c > 0xF4) return false; }
        else return false;
        if (i + need >= n) return false;
        // Check continuation bytes
        for (size_t k = 1; k <= need; k++) {
            unsigned char cc = b[i + k];
            if ((cc & 0xC0) != 0x80) return false;
        }
        // Basic overlong checks for 3/4-byte sequences
        if (need == 2) {
            unsigned char c1 = b[i+1];
            if (c == 0xE0 && (c1 & 0xE0) != 0xA0) return false; // overlong
            if (c == 0xED && (c1 & 0xE0) == 0xA0) return false; // surrogate range
        } else if (need == 3) {
            unsigned char c1 = b[i+1];
            if (c == 0xF0 && (c1 & 0xF0) != 0x90) return false; // overlong
            if (c == 0xF4 && (c1 & 0xF0) == 0xB0) return false; // > U+10FFFF
        }
        i += need + 1;
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: checkenc <file>\n");
        return 1;
    }
    const char *path = argv[1];
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "checkenc: cannot open %s\n", path); return 1; }
    unsigned char buf[8192];
    size_t n = fread(buf, 1, sizeof buf, fp);
    // Also get total size to improve heuristics
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fclose(fp);

    if (n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
        // UTF-8 BOM
        printf("UTF-8-BOM\n");
        return 0;
    }
    if (n >= 4 && buf[0] == 0xFF && buf[1] == 0xFE && buf[2] == 0x00 && buf[3] == 0x00) {
        printf("UTF-32-LE-BOM\n");
        return 0;
    }
    if (n >= 4 && buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0xFE && buf[3] == 0xFF) {
        printf("UTF-32-BE-BOM\n");
        return 0;
    }
    if (n >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
        printf("UTF-16-LE-BOM\n");
        return 0;
    }
    if (n >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) {
        printf("UTF-16-BE-BOM\n");
        return 0;
    }

    // No BOM: apply heuristics
    if (fsize == 0) { printf("ASCII\n"); return 0; }

    // ASCII: all bytes in sample < 0x80
    bool all_ascii = true;
    for (size_t i = 0; i < n; i++) { if (buf[i] >= 0x80) { all_ascii = false; break; } }
    if (all_ascii) { printf("ASCII\n"); return 0; }

    // UTF-32 LE: likely 00 00 00 non-zero or ascii in lowest byte repeatedly
    if (n >= 8) {
        int le32_hits = 0, be32_hits = 0;
        for (size_t i = 0; i + 3 < n; i += 4) {
            unsigned int b0 = buf[i], b1 = buf[i+1], b2 = buf[i+2], b3 = buf[i+3];
            if (b1 == 0 && b2 == 0 && b3 == 0 && b0 != 0) le32_hits++;
            if (b0 == 0 && b1 == 0 && b2 == 0 && b3 != 0) be32_hits++;
        }
        if (le32_hits > (int)(n/16)) { printf("UTF-32-LE\n"); return 0; }
        if (be32_hits > (int)(n/16)) { printf("UTF-32-BE\n"); return 0; }
    }

    // UTF-16: many zeros in even/odd positions
    if (n >= 4) {
        int le16_hits = 0, be16_hits = 0;
        for (size_t i = 0; i + 1 < n; i += 2) {
            unsigned int a = buf[i], b = buf[i+1];
            if (b == 0 && a != 0) le16_hits++;
            if (a == 0 && b != 0) be16_hits++;
        }
        if (le16_hits > (int)(n/8)) { printf("UTF-16-LE\n"); return 0; }
        if (be16_hits > (int)(n/8)) { printf("UTF-16-BE\n"); return 0; }
    }

    // UTF-8 without BOM: validate sequences
    if (is_valid_utf8(buf, n)) { printf("UTF-8\n"); return 0; }

    // Fallback
    printf("OTHER\n");
    return 0;
}
