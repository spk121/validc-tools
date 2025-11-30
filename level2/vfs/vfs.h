#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <time.h>

#define VFS_MAX_NAME 255
#define VFS_MAX_PATH 1024
#define VFS_PERM_READ    0x04
#define VFS_PERM_WRITE   0x02
#define VFS_PERM_EXEC    0x01
#define VFS_TYPE_FILE    1
#define VFS_TYPE_DIR     2
#define VFS_BLOCK_SIZE   4096
#define VFS_TOTAL_BLOCKS 256

typedef struct vfs_file VFS_FILE;
typedef struct vfs_dir VFS_DIR;
typedef struct vfs_fs VFS_FS;

typedef struct vfs_dirent {
    uint32_t d_ino;
    char d_name[VFS_MAX_NAME];
    uint8_t d_type;
} vfs_dirent;

typedef struct {
    uint32_t inode;
    char name[VFS_MAX_NAME];
    uint8_t type;
    uint16_t mode;
    uint32_t uid;
    uint32_t gid;
    time_t atime;
    time_t mtime;
    time_t ctime;
    uint64_t size;
    uint32_t block_index;
    uint32_t parent_inode;
    uint32_t child_count;
} vfs_inode;

VFS_FS* vfs_mount(const char* backing_file);
int vfs_unmount(VFS_FS* fs);
VFS_FILE* vfs_fopen(VFS_FS* fs, const char* path, const char* mode);
int vfs_fclose(VFS_FILE* file);
size_t vfs_fread(void* ptr, size_t size, size_t nmemb, VFS_FILE* file);
size_t vfs_fwrite(const void* ptr, size_t size, size_t nmemb, VFS_FILE* file);
int vfs_fseek(VFS_FILE* file, long offset, int whence);
long vfs_ftell(VFS_FILE* file);
int vfs_mkdir(VFS_FS* fs, const char* path, uint16_t mode);
int vfs_remove(VFS_FS* fs, const char* path);
int vfs_rename(VFS_FS* fs, const char* old_path, const char* new_path);
VFS_DIR* vfs_opendir(VFS_FS* fs, const char* path);
int vfs_closedir(VFS_DIR* dir);
struct vfs_dirent* vfs_readdir(VFS_DIR* dir);
int vfs_readdir_r(VFS_DIR* dir, struct vfs_dirent* entry, struct vfs_dirent** result);
void vfs_rewinddir(VFS_DIR* dir);

#endif
