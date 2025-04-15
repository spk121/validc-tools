#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define BUFFER_SIZE 4096 // 4 KB chunks, matching VFS cluster size

void copy_vfs_to_real(const char* vfs_path, const char* real_path) {
    // Check if virtual file exists
    struct stat vfs_statbuf;
    if (vfs_stat(vfs_path, &vfs_statbuf) != 0) {
        fprintf(stderr, "Warning: Virtual file '%s' does not exist (errno: %d)\n", vfs_path, errno);
        return;
    }
    if (vfs_statbuf.st_mode & S_IFDIR) {
        fprintf(stderr, "Warning: Virtual path '%s' is a directory, not a file\n", vfs_path);
        return;
    }

    // Check if real file already exists
    FILE* real_check = fopen(real_path, "r");
    if (real_check != NULL) {
        fclose(real_check);
        fprintf(stderr, "Warning: Real file '%s' already exists\n", real_path);
        return;
    }

    // Open virtual file for reading
    int vfs_fd = vfs_open(vfs_path, O_RDONLY);
    if (vfs_fd < 0) {
        fprintf(stderr, "Error: Failed to open virtual file '%s' (errno: %d)\n", vfs_path, errno);
        return;
    }

    // Open real file for writing
    FILE* real_file = fopen(real_path, "wb");
    if (!real_file) {
        fprintf(stderr, "Error: Failed to open real file '%s' (errno: %d)\n", real_path, errno);
        vfs_close(vfs_fd);
        return;
    }

    // Copy file in chunks
    uint8_t buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    size_t total_bytes = 0;

    while ((bytes_read = vfs_read(vfs_fd, buffer, BUFFER_SIZE)) > 0) {
        size_t bytes_written = fwrite(buffer, 1, bytes_read, real_file);
        if (bytes_written != (size_t)bytes_read) {
            fprintf(stderr, "Error: Failed to write to real file '%s' (errno: %d)\n", real_path, errno);
            fclose(real_file);
            vfs_close(vfs_fd);
            return;
        }
        total_bytes += bytes_written;
    }

    if (bytes_read < 0) {
        fprintf(stderr, "Error: Failed to read from virtual file '%s' (errno: %d)\n", vfs_path, errno);
        fclose(real_file);
        vfs_close(vfs_fd);
        return;
    }

    // Verify total bytes match file size
    if (total_bytes != vfs_statbuf.st_size) {
        fprintf(stderr, "Warning: Copied %zu bytes, expected %ld bytes\n", total_bytes, (long)vfs_statbuf.st_size);
    } else {
        printf("Successfully copied '%s' to '%s' (%zu bytes)\n", vfs_path, real_path, total_bytes);
    }

    // Cleanup
    fclose(real_file);
    vfs_close(vfs_fd);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <vfs_path> <real_path>\n", argv[0]);
        fprintf(stderr, "Example: %s /dir1/file.txt /mnt/destination.txt\n", argv[0]);
        return 1;
    }

    const char* vfs_path = argv[1];
    const char* real_path = argv[2];

    // Ensure real path starts with /mnt
    if (strncmp(real_path, "/mnt", 4) != 0) {
        fprintf(stderr, "Error: Real path must start with '/mnt'\n");
        return 1;
    }

    // Initialize and mount VFS (assuming flash.bin exists)
    FILE* flash = fopen("flash.bin", "r+b");
    if (!flash) {
        perror("Failed to open flash.bin");
        return 1;
    }
    vfs_init();
    if (vfs_mount(flash) != 0) {
        fprintf(stderr, "Failed to mount VFS\n");
        fclose(flash);
        return 1;
    }

    // Perform the copy
    copy_vfs_to_real(vfs_path, real_path);

    // Flush VFS and cleanup
    vfs_flush();
    fclose(flash);

    return 0;
}
