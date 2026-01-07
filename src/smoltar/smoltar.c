/*
 * smoltar - A simple tar-like archiver using ISO C only
 * Uses a modified ustar format
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define BLOCK_SIZE 512
#define NAME_SIZE 100
#define MODE_SIZE 8
#define UID_SIZE 8
#define GID_SIZE 8
#define SIZE_SIZE 12
#define MTIME_SIZE 12
#define CHKSUM_SIZE 8
#define TYPEFLAG_SIZE 1
#define LINKNAME_SIZE 100
#define MAGIC_SIZE 6
#define VERSION_SIZE 2
#define UNAME_SIZE 32
#define GNAME_SIZE 32
#define DEVMAJOR_SIZE 8
#define DEVMINOR_SIZE 8
#define PREFIX_SIZE 155
#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE 65  /* 64 hex chars + null terminator */

/* Extraction modes */
typedef enum {
    MODE_FORCE,   /* Always extract, overwrite existing files */
    MODE_NORMAL,  /* Extract only if SHA-256 differs or file doesn't exist */
    MODE_SAFE     /* Never overwrite existing files */
} extract_mode_t;

/* Tar header structure (512 bytes) */
struct tar_header {
    char name[NAME_SIZE];       /* 0   - file name */
    char mode[MODE_SIZE];       /* 100 - file mode (octal) */
    char uid[UID_SIZE];         /* 108 - user ID (octal) */
    char gid[GID_SIZE];         /* 116 - group ID (octal) */
    char size[SIZE_SIZE];       /* 124 - file size (octal) */
    char mtime[MTIME_SIZE];     /* 136 - modification time (octal) */
    char chksum[CHKSUM_SIZE];   /* 148 - checksum (octal) */
    char typeflag;              /* 156 - type flag */
    char linkname[LINKNAME_SIZE]; /* 157 - link name */
    char magic[MAGIC_SIZE];     /* 257 - "ustar" */
    char version[VERSION_SIZE]; /* 263 - "00" */
    char uname[UNAME_SIZE];     /* 265 - user name */
    char gname[GNAME_SIZE];     /* 297 - group name */
    char devmajor[DEVMAJOR_SIZE]; /* 329 - device major number */
    char devminor[DEVMINOR_SIZE]; /* 337 - device minor number */
    char prefix[PREFIX_SIZE];   /* 345 - prefix for long names */
    char padding[12];           /* 500 - padding to 512 bytes */
};

/* SHA-256 context structure */
struct sha256_ctx {
    unsigned long state[8];
    unsigned long count[2];
    unsigned char buffer[64];
};

/* SHA-256 constants */
static const unsigned long sha256_k[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

/* SHA-256 helper macros */
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHR(x, n) ((x) >> (n))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

/* SHA-256 transform function */
static void sha256_transform(struct sha256_ctx *ctx, const unsigned char data[64]) {
    unsigned long w[64];
    unsigned long a, b, c, d, e, f, g, h;
    unsigned long t1, t2;
    int i, j;
    
    /* Prepare message schedule */
    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        w[i] = ((unsigned long)data[j] << 24) |
               ((unsigned long)data[j + 1] << 16) |
               ((unsigned long)data[j + 2] << 8) |
               ((unsigned long)data[j + 3]);
    }
    for (; i < 64; ++i) {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }
    
    /* Initialize working variables */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    
    /* Main loop */
    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + w[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    
    /* Add compressed chunk to current hash value */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/* Initialize SHA-256 context */
static void sha256_init(struct sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667UL;
    ctx->state[1] = 0xbb67ae85UL;
    ctx->state[2] = 0x3c6ef372UL;
    ctx->state[3] = 0xa54ff53aUL;
    ctx->state[4] = 0x510e527fUL;
    ctx->state[5] = 0x9b05688cUL;
    ctx->state[6] = 0x1f83d9abUL;
    ctx->state[7] = 0x5be0cd19UL;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

/* Update SHA-256 context with data */
static void sha256_update(struct sha256_ctx *ctx, const unsigned char *data, size_t len) {
    size_t i;
    unsigned long index = (ctx->count[0] >> 3) & 0x3f;
    
    ctx->count[0] += (unsigned long)(len << 3);
    if (ctx->count[0] < (unsigned long)(len << 3)) {
        ctx->count[1]++;
    }
    ctx->count[1] += (unsigned long)(len >> 29);
    
    for (i = 0; i < len; ++i) {
        ctx->buffer[index++] = data[i];
        if (index == 64) {
            sha256_transform(ctx, ctx->buffer);
            index = 0;
        }
    }
}

/* Finalize SHA-256 and produce digest */
static void sha256_final(struct sha256_ctx *ctx, unsigned char digest[SHA256_DIGEST_SIZE]) {
    unsigned char bits[8];
    unsigned long index, padlen;
    int i, j;
    
    /* Save number of bits */
    for (i = 0; i < 4; ++i) {
        bits[i] = (unsigned char)((ctx->count[1] >> (24 - i * 8)) & 0xff);
        bits[i + 4] = (unsigned char)((ctx->count[0] >> (24 - i * 8)) & 0xff);
    }
    
    /* Pad out to 56 mod 64 */
    index = (ctx->count[0] >> 3) & 0x3f;
    padlen = (index < 56) ? (56 - index) : (120 - index);
    {
        unsigned char padding[64];
        memset(padding, 0, sizeof(padding));
        padding[0] = 0x80;
        sha256_update(ctx, padding, padlen);
    }
    
    /* Append length */
    sha256_update(ctx, bits, 8);
    
    /* Store state in digest */
    for (i = 0; i < 8; ++i) {
        for (j = 0; j < 4; ++j) {
            digest[i * 4 + j] = (unsigned char)((ctx->state[i] >> (24 - j * 8)) & 0xff);
        }
    }
}

/* Convert digest to hex string */
static void sha256_to_hex(const unsigned char digest[SHA256_DIGEST_SIZE], char hex[SHA256_HEX_SIZE]) {
    static const char hexchars[] = "0123456789abcdef";
    int i;
    for (i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        hex[i * 2] = hexchars[digest[i] >> 4];
        hex[i * 2 + 1] = hexchars[digest[i] & 0x0f];
    }
    hex[SHA256_HEX_SIZE - 1] = '\0';
}

/* Compute SHA-256 of a file */
static bool compute_file_sha256(FILE *fp, char hex[SHA256_HEX_SIZE]) {
    struct sha256_ctx ctx;
    unsigned char buffer[BLOCK_SIZE];
    unsigned char digest[SHA256_DIGEST_SIZE];
    size_t n;
    
    sha256_init(&ctx);
    
    while ((n = fread(buffer, 1, BLOCK_SIZE, fp)) > 0) {
        sha256_update(&ctx, buffer, n);
    }
    
    if (ferror(fp)) {
        return false;
    }
    
    sha256_final(&ctx, digest);
    sha256_to_hex(digest, hex);
    return true;
}

/* Format an octal string */
static void format_octal(char *dest, unsigned long value, size_t size) {
    size_t i;
    dest[size - 1] = '\0';
    /* Fill digits from right to left, stopping before position 0 */
    for (i = size - 2; i > 0; i--) {
        dest[i] = '0' + (value & 7);
        value >>= 3;
    }
    /* Handle position 0 separately to check for overflow */
    if (value > 7) {
        /* Value is too large, fill with maximum octal value */
        for (i = 0; i < size - 1; i++) {
            dest[i] = '7';
        }
    } else {
        dest[0] = '0' + (value & 7);
    }
}

/* Parse an octal string */
static unsigned long parse_octal(const char *str, size_t size) {
    unsigned long result = 0;
    size_t i;
    for (i = 0; i < size && str[i] >= '0' && str[i] <= '7'; i++) {
        result = (result << 3) | (str[i] - '0');
    }
    return result;
}

/* Calculate checksum for tar header */
static unsigned long calculate_checksum(const struct tar_header *header) {
    unsigned long sum = 0;
    const unsigned char *bytes = (const unsigned char *)header;
    size_t i;
    
    /* Sum all bytes, treating checksum field as spaces */
    for (i = 0; i < BLOCK_SIZE; i++) {
        if (i >= 148 && i < 156) {
            sum += ' ';
        } else {
            sum += bytes[i];
        }
    }
    return sum;
}

/* Initialize a tar header */
static void init_header(struct tar_header *header, const char *filename, 
                       unsigned long filesize, const char *sha256_hex) {
    time_t now;
    const char *p;
    
    memset(header, 0, sizeof(*header));
    
    /* Validate filename: must not contain any path separators */
    for (p = filename; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            fprintf(stderr, "Error: filename must not contain path separators: %s\n", filename);
            exit(EXIT_FAILURE);
        }
    }
    
    /* Set filename (must not have path elements) */
    strncpy(header->name, filename, NAME_SIZE - 1);
    
    /* Set mode to 644 (owner: rw, group: r, other: r) */
    format_octal(header->mode, 0644, MODE_SIZE);
    
    /* Set uid and gid to 0 */
    format_octal(header->uid, 0, UID_SIZE);
    format_octal(header->gid, 0, GID_SIZE);
    
    /* Set file size */
    format_octal(header->size, filesize, SIZE_SIZE);
    
    /* Set modification time to current time */
    now = time(NULL);
    format_octal(header->mtime, (unsigned long)now, MTIME_SIZE);
    
    /* Set type flag to regular file */
    header->typeflag = '0';
    
    /* Set SHA-256 hash in linkname field (64 hex chars + null terminator) */
    if (sha256_hex != NULL) {
        strncpy(header->linkname, sha256_hex, LINKNAME_SIZE - 1);
    }
    
    /* Set ustar magic and version */
    memcpy(header->magic, "ustar", 5);
    header->magic[5] = '\0';
    header->version[0] = '0';
    header->version[1] = '0';
    
    /* Calculate and set checksum */
    /* Standard ustar format: 6-digit octal, null byte, space */
    memset(header->chksum, ' ', CHKSUM_SIZE);
    format_octal(header->chksum, calculate_checksum(header), CHKSUM_SIZE);
    header->chksum[CHKSUM_SIZE - 1] = ' ';
}

/* Get file size using fseek/ftell */
static long get_file_size(FILE *fp) {
    long size;
    if (fseek(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    size = ftell(fp);
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return -1;
    }
    return size;
}

/* Add a file to archive */
static bool add_file_to_archive(FILE *archive, const char *filename) {
    FILE *input;
    struct tar_header header;
    long filesize;
    long bytes_written;
    size_t padding;
    char buffer[BLOCK_SIZE];
    char sha256_hex[SHA256_HEX_SIZE];
    size_t n;
    
    /* Open input file */
    input = fopen(filename, "rb");
    if (input == NULL) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return false;
    }
    
    /* Get file size */
    filesize = get_file_size(input);
    if (filesize < 0) {
        fprintf(stderr, "Error: Cannot determine size of file '%s'\n", filename);
        fclose(input);
        return false;
    }
    
    /* Compute SHA-256 hash */
    if (!compute_file_sha256(input, sha256_hex)) {
        fprintf(stderr, "Error: Cannot compute SHA-256 for '%s'\n", filename);
        fclose(input);
        return false;
    }
    
    /* Reset file position after computing hash */
    if (fseek(input, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Cannot reset file position for '%s'\n", filename);
        fclose(input);
        return false;
    }
    
    /* Initialize and write header */
    init_header(&header, filename, (unsigned long)filesize, sha256_hex);
    if (fwrite(&header, 1, BLOCK_SIZE, archive) != BLOCK_SIZE) {
        fprintf(stderr, "Error: Cannot write header for '%s'\n", filename);
        fclose(input);
        return false;
    }
    
    /* Write file content */
    bytes_written = 0;
    while ((n = fread(buffer, 1, BLOCK_SIZE, input)) > 0) {
        if (fwrite(buffer, 1, n, archive) != n) {
            fprintf(stderr, "Error: Cannot write data for '%s'\n", filename);
            fclose(input);
            return false;
        }
        bytes_written += n;
    }
    
    fclose(input);
    
    /* Write padding to align to block size */
    padding = (size_t)(BLOCK_SIZE - (filesize % BLOCK_SIZE));
    if (padding != BLOCK_SIZE) {
        memset(buffer, 0, padding);
        if (fwrite(buffer, 1, padding, archive) != padding) {
            fprintf(stderr, "Error: Cannot write padding for '%s'\n", filename);
            return false;
        }
    }
    
    return true;
}

/* Create archive */
static bool create_archive(const char *archive_name, char **filenames, int count) {
    FILE *archive;
    int i;
    char end_marker[BLOCK_SIZE * 2];
    
    archive = fopen(archive_name, "wb");
    if (archive == NULL) {
        fprintf(stderr, "Error: Cannot create archive '%s'\n", archive_name);
        return false;
    }
    
    /* Add each file to archive */
    for (i = 0; i < count; i++) {
        if (!add_file_to_archive(archive, filenames[i])) {
            fclose(archive);
            return false;
        }
    }
    
    /* Write end-of-archive marker (two zero blocks) */
    memset(end_marker, 0, sizeof(end_marker));
    if (fwrite(end_marker, 1, sizeof(end_marker), archive) != sizeof(end_marker)) {
        fprintf(stderr, "Error: Cannot write end-of-archive marker\n");
        fclose(archive);
        return false;
    }
    
    fclose(archive);
    printf("Archive '%s' created successfully\n", archive_name);
    return true;
}

/* Extract a file from archive */
static bool extract_file(FILE *archive, const struct tar_header *header, 
                        extract_mode_t mode, const char *prefix) {
    FILE *output;
    FILE *existing;
    unsigned long filesize;
    unsigned long bytes_remaining;
    unsigned long padding;
    char buffer[BLOCK_SIZE];
    char output_filename[NAME_SIZE + PREFIX_SIZE];
    char existing_sha256[SHA256_HEX_SIZE];
    char archive_sha256[SHA256_HEX_SIZE + 1];  /* +1 for safety */
    size_t to_read;
    size_t n;
    bool should_extract = true;
    
    filesize = parse_octal(header->size, SIZE_SIZE);
    
    /* Build output filename with optional prefix */
    if (prefix != NULL && prefix[0] != '\0') {
        snprintf(output_filename, sizeof(output_filename), "%s%s", prefix, header->name);
    } else {
        strncpy(output_filename, header->name, sizeof(output_filename) - 1);
        output_filename[sizeof(output_filename) - 1] = '\0';
    }
    
    /* Get SHA-256 from archive header */
    strncpy(archive_sha256, header->linkname, SHA256_HEX_SIZE);
    archive_sha256[SHA256_HEX_SIZE] = '\0';
    
    /* Handle different extraction modes */
    if (mode == MODE_SAFE) {
        /* Check if file exists */
        existing = fopen(output_filename, "rb");
        if (existing != NULL) {
            fclose(existing);
            fprintf(stderr, "Skipping '%s': file exists (safe mode)\n", output_filename);
            should_extract = false;
        }
    } else if (mode == MODE_NORMAL && archive_sha256[0] != '\0') {
        /* Check if file exists and compare SHA-256 */
        existing = fopen(output_filename, "rb");
        if (existing != NULL) {
            if (compute_file_sha256(existing, existing_sha256)) {
                if (strcmp(existing_sha256, archive_sha256) == 0) {
                    fprintf(stderr, "Skipping '%s': identical (SHA-256 match)\n", output_filename);
                    should_extract = false;
                }
            }
            fclose(existing);
        }
    }
    /* MODE_FORCE always extracts */
    
    /* Skip file data if not extracting */
    if (!should_extract) {
        padding = ((filesize + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
        if (fseek(archive, padding, SEEK_CUR) != 0) {
            fprintf(stderr, "Error: Cannot skip file content for '%s'\n", header->name);
            return false;
        }
        return true;
    }
    
    /* Open output file */
    output = fopen(output_filename, "wb");
    if (output == NULL) {
        fprintf(stderr, "Error: Cannot create file '%s'\n", output_filename);
        /* Skip the file data in archive */
        padding = ((filesize + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
        fseek(archive, padding, SEEK_CUR);
        return false;
    }
    
    /* Write file content */
    bytes_remaining = filesize;
    while (bytes_remaining > 0) {
        to_read = bytes_remaining > BLOCK_SIZE ? BLOCK_SIZE : bytes_remaining;
        n = fread(buffer, 1, to_read, archive);
        if (n != to_read) {
            fprintf(stderr, "Error: Cannot read data for '%s'\n", header->name);
            fclose(output);
            return false;
        }
        if (fwrite(buffer, 1, n, output) != n) {
            fprintf(stderr, "Error: Cannot write data to '%s'\n", output_filename);
            fclose(output);
            return false;
        }
        bytes_remaining -= n;
    }
    
    fclose(output);
    
    /* Skip padding */
    padding = BLOCK_SIZE - (filesize % BLOCK_SIZE);
    if (padding != BLOCK_SIZE) {
        if (fread(buffer, 1, padding, archive) != padding) {
            fprintf(stderr, "Error: Cannot skip padding for '%s'\n", header->name);
            return false;
        }
    }
    
    return true;
}

/* Extract archive */
static bool extract_archive(const char *archive_name, extract_mode_t mode, const char *prefix) {
    FILE *archive;
    struct tar_header header;
    size_t bytes_read;
    unsigned long checksum;
    unsigned long stored_checksum;
    
    archive = fopen(archive_name, "rb");
    if (archive == NULL) {
        fprintf(stderr, "Error: Cannot open archive '%s'\n", archive_name);
        return false;
    }
    
    /* Process each file in archive */
    while ((bytes_read = fread(&header, 1, BLOCK_SIZE, archive)) == BLOCK_SIZE) {
        /* Check for end-of-archive (zero block) */
        if (header.name[0] == '\0') {
            break;
        }
        
        /* Verify magic */
        if (strncmp(header.magic, "ustar", 5) != 0) {
            fprintf(stderr, "Error: Invalid archive format\n");
            fclose(archive);
            return false;
        }
        
        /* Verify checksum */
        stored_checksum = parse_octal(header.chksum, CHKSUM_SIZE);
        checksum = calculate_checksum(&header);
        if (checksum != stored_checksum) {
            fprintf(stderr, "Error: Checksum mismatch for '%s'\n", header.name);
            fclose(archive);
            return false;
        }
        
        printf("Extracting: %s\n", header.name);
        if (!extract_file(archive, &header, mode, prefix)) {
            fclose(archive);
            return false;
        }
    }
    
    fclose(archive);
    printf("Extraction completed\n");
    return true;
}

/* List archive contents */
static bool list_archive(const char *archive_name) {
    FILE *archive;
    struct tar_header header;
    size_t bytes_read;
    unsigned long filesize;
    unsigned long padding;
    
    archive = fopen(archive_name, "rb");
    if (archive == NULL) {
        fprintf(stderr, "Error: Cannot open archive '%s'\n", archive_name);
        return false;
    }
    
    printf("Contents of archive '%s':\n", archive_name);
    printf("%-40s %12s\n", "Name", "Size");
    printf("%-40s %12s\n", "----", "----");
    
    /* Process each file in archive */
    while ((bytes_read = fread(&header, 1, BLOCK_SIZE, archive)) == BLOCK_SIZE) {
        /* Check for end-of-archive (zero block) */
        if (header.name[0] == '\0') {
            break;
        }
        
        /* Verify magic */
        if (strncmp(header.magic, "ustar", 5) != 0) {
            fprintf(stderr, "Error: Invalid archive format\n");
            fclose(archive);
            return false;
        }
        
        filesize = parse_octal(header.size, SIZE_SIZE);
        printf("%-40s %12lu\n", header.name, filesize);
        
        /* Skip file content and padding */
        padding = ((filesize + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
        if (fseek(archive, padding, SEEK_CUR) != 0) {
            fprintf(stderr, "Error: Cannot skip file content\n");
            fclose(archive);
            return false;
        }
    }
    
    fclose(archive);
    return true;
}

/* Print usage information */
static void print_help(void) {
    printf("Usage: smoltar [options] archive [files...]\n");
    printf("A simple tar-like archiver using ISO C only\n\n");
    printf("Options:\n");
    printf("  -c           Create a new archive\n");
    printf("  -x           Extract files from archive\n");
    printf("  -t           List contents of archive\n");
    printf("  -f FILE      Specify archive file (required)\n");
    printf("  -m MODE      Extraction mode: force (default), normal, safe\n");
    printf("                 force:  Always extract, overwrite existing files\n");
    printf("                 normal: Extract only if SHA-256 differs or file missing\n");
    printf("                 safe:   Never overwrite existing files\n");
    printf("  -p PREFIX    Prepend PREFIX to extracted filenames\n");
    printf("  -h           Display this help message\n\n");
    printf("Examples:\n");
    printf("  smoltar -cf archive.tar file1.txt file2.txt\n");
    printf("  smoltar -xf archive.tar\n");
    printf("  smoltar -xf archive.tar -m normal\n");
    printf("  smoltar -xf archive.tar -p output/\n");
    printf("  smoltar -tf archive.tar\n");
}

int main(int argc, char *argv[]) {
    int i;
    char *archive_name = NULL;
    char *prefix = NULL;
    extract_mode_t mode = MODE_FORCE;
    bool create = false;
    bool extract = false;
    bool list = false;
    int first_file_arg;
    
    /* Parse command line arguments */
    if (argc < 2) {
        print_help();
        return 1;
    }
    
    /* Parse options */
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        const char *opt = argv[i];
        size_t j;
        
        if (strcmp(opt, "-h") == 0 || strcmp(opt, "--help") == 0) {
            print_help();
            return 0;
        }
        
        /* Handle long-form options */
        if (strcmp(opt, "-m") == 0) {
            if (i + 1 < argc) {
                const char *mode_str = argv[++i];
                if (strcmp(mode_str, "force") == 0) {
                    mode = MODE_FORCE;
                } else if (strcmp(mode_str, "normal") == 0) {
                    mode = MODE_NORMAL;
                } else if (strcmp(mode_str, "safe") == 0) {
                    mode = MODE_SAFE;
                } else {
                    fprintf(stderr, "Error: Invalid mode '%s'\n", mode_str);
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: -m option requires an argument\n");
                return 1;
            }
            continue;
        }
        
        if (strcmp(opt, "-p") == 0) {
            if (i + 1 < argc) {
                prefix = argv[++i];
            } else {
                fprintf(stderr, "Error: -p option requires an argument\n");
                return 1;
            }
            continue;
        }
        
        /* Parse combined options like -cf or -xf */
        for (j = 1; opt[j] != '\0'; j++) {
            switch (opt[j]) {
                case 'c':
                    create = true;
                    break;
                case 'x':
                    extract = true;
                    break;
                case 't':
                    list = true;
                    break;
                case 'f':
                    /* Archive name is next argument */
                    if (j == strlen(opt) - 1) {
                        /* -f is last in this option group, name is next arg */
                        if (i + 1 < argc) {
                            archive_name = argv[++i];
                        }
                        break;
                    } else {
                        /* Name immediately follows -f */
                        archive_name = argv[i] + j + 1;
                        j = strlen(opt) - 1; /* Will increment to strlen(opt), terminating loop */
                        break;
                    }
                case 'm':
                case 'p':
                    fprintf(stderr, "Error: -%c option cannot be combined\n", opt[j]);
                    return 1;
                default:
                    fprintf(stderr, "Error: Unknown option '-%c'\n", opt[j]);
                    print_help();
                    return 1;
            }
        }
    }
    
    /* Validate options */
    if (archive_name == NULL) {
        fprintf(stderr, "Error: Archive name not specified (use -f option)\n");
        return 1;
    }
    
    if ((create ? 1 : 0) + (extract ? 1 : 0) + (list ? 1 : 0) != 1) {
        fprintf(stderr, "Error: Exactly one of -c, -x, or -t must be specified\n");
        return 1;
    }
    
    first_file_arg = i;
    
    /* Execute requested operation */
    if (create) {
        if (first_file_arg >= argc) {
            fprintf(stderr, "Error: No files specified for archive creation\n");
            return 1;
        }
        return create_archive(archive_name, argv + first_file_arg, 
                            argc - first_file_arg) ? 0 : 1;
    } else if (extract) {
        return extract_archive(archive_name, mode, prefix) ? 0 : 1;
    } else if (list) {
        return list_archive(archive_name) ? 0 : 1;
    }
    
    return 1;
}
