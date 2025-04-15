#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>

#define O_RDONLY 1
#define O_WRONLY 2
#define O_RDWR   3
#define O_CREAT  4

#define DT_DIR  4
#define DT_REG  8

#define S_IFDIR 0040000 // Directory
#define S_IFREG 0100000 // Regular file

#define ENOENT  2  // No such file or directory
#define EBADF   9  // Bad file descriptor
#define ENOSPC 28  // No space left on device
#define EEXIST 17  // File exists
#define ENOTDIR 20 // Not a directory
#define EISDIR  21 // Is a directory
#define ENOTEMPTY 39 // Directory not empty

struct dirent {
    char d_name[12];  // 8.3 name + null terminator
    uint8_t d_type;   // DT_DIR or DT_REG
};

struct stat {
    mode_t st_mode;    // File type (S_IFDIR or S_IFREG)
    off_t st_size;     // File size in bytes
    time_t st_mtime;   // Last modification time
};

typedef struct DirHandle DIR;

int vfs_init(void);
int vfs_mount(FILE* flash);
int vfs_flush(void);
int vfs_open(const char* path, int mode);
int vfs_close(int fd);
ssize_t vfs_read(int fd, void* buf, size_t count);
ssize_t vfs_write(int fd, const void* buf, size_t count);
int vfs_unlink(const char* path);
DIR* vfs_opendir(const char* path);
struct dirent* vfs_readdir(DIR* dirp);
int vfs_closedir(DIR* dirp);
int vfs_mkdir(const char* path, uint32_t mode);
int vfs_rmdir(const char* path);
int vfs_stat(const char* path, struct stat* statbuf);

extern int errno;

#endif
