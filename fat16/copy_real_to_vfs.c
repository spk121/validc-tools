#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define BUFFER_SIZE 4096 // 4 KB chunks, matching VFS cluster size

void copy_real_to_vfs(const char* real_path, const char* vfs_path) {
    // Check if real file exists
    FILE* real_check = fopen(real_path, "r");
    if (!real_check) {
        fprintf(stderr, "Warning: Real file '%s' does not exist (errno: %d)\n", real_path, errno);
        return;
    }
    fclose(real_check);

    // Check if virtual file already exists
    struct stat vfs_statbuf;
    if (vfs_stat(vfs_path, &vfs_statbuf) == 0) {
        if (vfs_statbuf.st_mode & S_IFDIR) {
            fprintf(stderr, "Warning: Virtual path '%s' is a directory, not a file\n", vfs_path);
        } else {
            fprintf(stderr, "Warning: Virtual file '%s' already exists\n", vfs_path);
        }
        return;
    }

    // Open real file for reading
    FILE* real_file = fopen(real_path, "rb");
    if (!real_file) {
        fprintf(stderr, "Error: Failed to open real file '%s' (errno: %d)\n", real_path, errno);
        return;
    }

    // Open virtual file for writing (create if it doesn't exist)
    int vfs_fd = vfs_open(vfs_path, O_CREAT | O_WRONLY);
    if (vfs_fd < 0) {
        fprintf(stderr, "Error: Failed to open virtual file '%s' (errno: %d)\n", vfs_path, errno);
        fclose(real_file);
        return;
    }

    // Copy file in chunks
    uint8_t buffer[BUFFER_SIZE];
    size_t bytes_read;
    size_t total_bytes = 0;

    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, real_file)) > 0) {
        ssize_t bytes_written = vfs_write(vfs_fd, buffer, bytes_read);
        if (bytes_written != (ssize_t)bytes_read) {
            fprintf(stderr, "Error: Failed to write to virtual file '%s' (errno: %d)\n", vfs_path, errno);
            vfs_close(vfs_fd);
            fclose(real_file);
            return;
        }
        total_bytes += bytes_written;
    }

    if (ferror(real_file)) {
        fprintf(stderr, "Error: Failed to read from real file '%s' (errno: %d)\n", real_path, errno);
        vfs_close(vfs_fd);
        fclose(real_file);
        return;
    }

    // Get real file size for verification
    fseek(real_file, 0, SEEK_END);
    long real_size = ftell(real_file);
    if (total_bytes != (size_t)real_size) {
        fprintf(stderr, "Warning: Copied %zu bytes, expected %ld bytes\n", total_bytes, real_size);
    } else {
        printf("Successfully copied '%s' to '%s' (%zu bytes)\n", real_path, vfs_path, total_bytes);
    }

    // Cleanup
    vfs_close(vfs_fd);
    fclose(real_file);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <real_path> <vfs_path>\n", argv[0]);
        fprintf(stderr, "Example: %s /mnt/source.txt /dir1/destination.txt\n", argv[0]);
        return 1;
    }

    const char* real_path = argv[1];
    const char* vfs_path = argv[2];

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
    copy_real_to_vfs(real_path, vfs_path);

    // Flush VFS and cleanup
    vfs_flush();
    fclose(flash);

    return 0;
}
