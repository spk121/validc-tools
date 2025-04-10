#include "vfs_client.h"
#include <stdio.h>

int main() {
    VFS_FILE* file = vfs_fopen("/test.txt", "w");
    if (!file) {
        printf("Failed to open file\n");
        return 1;
    }
    vfs_fwrite("Hello, VFS!", 1, 12, file);
    vfs_fclose(file);

    file = vfs_fopen("/test.txt", "r");
    char buffer[13] = {0};
    vfs_fread(buffer, 1, 12, file);
    printf("Read: %s\n", buffer);
    vfs_fclose(file);

    vfs_mkdir("/dir", 0755);
    VFS_DIR* dir = vfs_opendir("/dir");
    vfs_closedir(dir);
    return 0;
}
