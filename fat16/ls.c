#include "vfs.h"
#include <stdio.h>
#include <string.h>

void ls_l(const char* dir_path) {
    DIR* dir = vfs_opendir(dir_path);
    if (!dir) {
        printf("Cannot open directory: %d\n", errno);
        return;
    }

    struct dirent* ent;
    while ((ent = vfs_readdir(dir))) {
        char full_path[MAX_PATH];
        snprintf(full_path, MAX_PATH, "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (vfs_stat(full_path, &st) != 0) {
            printf("Stat failed for %s: %d\n", ent->d_name, errno);
            continue;
        }

        // Format time
        char time_str[26];
        struct tm* tm = localtime(&st.st_mtime);
        strftime(time_str, sizeof(time_str), "%b %d %H:%M", tm);

        // Print in ls -l style (simplified, no perms/owner)
        printf("%c %10ld %s %s\n",
               (st.st_mode & S_IFDIR) ? 'd' : '-',
               (long)st.st_size,
               time_str,
               ent->d_name);
    }

    vfs_closedir(dir);
}

int main() {
    FILE* flash = fopen("flash.bin", "r+b");
    if (!flash) {
        perror("Failed to open flash");
        return 1;
    }

    vfs_init();
    vfs_mount(flash);

    vfs_mkdir("/dir1", 0755);
    int fd = vfs_open("/dir1/file.txt", O_CREAT | O_WRONLY);
    vfs_write(fd, "Hello, world!", 13);
    vfs_close(fd);

    printf("Listing /dir1:\n");
    ls_l("/dir1");

    vfs_flush();
    fclose(flash);
    return 0;
}
