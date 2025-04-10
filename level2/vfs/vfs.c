#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vfs.h"

typedef struct {
    uint32_t magic;
    uint64_t size;
    uint32_t inode_count;
    uint32_t free_blocks;
    uint64_t data_start;
    uint32_t block_map[VFS_TOTAL_BLOCKS];
} vfs_superblock;

struct vfs_fs {
    FILE* backing_file;
    vfs_superblock sb;
    vfs_inode* inodes;
};

struct vfs_file {
    VFS_FS* fs;
    vfs_inode* inode;
    uint64_t offset;
    char mode[3];
};

struct vfs_dir {
    VFS_FS* fs;
    vfs_inode* dir_inode;
    uint32_t current_child;
};

#define VFS_MAGIC 0xVF51

static vfs_inode* find_inode_by_path(VFS_FS* fs, const char* path) {
    if (strcmp(path, "/") == 0) return &fs->inodes[0];

    char* path_copy = strdup(path);
    char* token = strtok(path_copy, "/");
    vfs_inode* current = &fs->inodes[0];

    while (token) {
        int found = 0;
        for (uint32_t i = 0; i < fs->sb.inode_count; i++) {
            if (fs->inodes[i].parent_inode == current->inode &&
                strcmp(fs->inodes[i].name, token) == 0) {
                current = &fs->inodes[i];
                found = 1;
                break;
            }
        }
        if (!found) {
            free(path_copy);
            return NULL;
        }
        token = strtok(NULL, "/");
    }
    free(path_copy);
    return current;
}

static uint32_t allocate_block(VFS_FS* fs) {
    for (uint32_t i = 0; i < VFS_TOTAL_BLOCKS; i++) {
        if (fs->sb.block_map[i] == 0) {
            fs->sb.free_blocks--;
            return i;
        }
    }
    return UINT32_MAX;
}

static void free_block(VFS_FS* fs, uint32_t block_index) {
    if (block_index < VFS_TOTAL_BLOCKS && fs->sb.block_map[block_index] != 0) {
        fs->sb.block_map[block_index] = 0;
        fs->sb.free_blocks++;
    }
}

static int resize_inodes(VFS_FS* fs) {
    vfs_inode* new_inodes = realloc(fs->inodes, (fs->sb.inode_count + 1) * sizeof(vfs_inode));
    if (!new_inodes) return -1;
    fs->inodes = new_inodes;
    return 0;
}

VFS_FS* vfs_mount(const char* backing_file) {
    VFS_FS* fs = malloc(sizeof(VFS_FS));
    if (!fs) return NULL;

    fs->backing_file = fopen(backing_file, "r+b");
    if (!fs->backing_file) {
        fs->backing_file = fopen(backing_file, "w+b");
        if (!fs->backing_file) {
            free(fs);
            return NULL;
        }
        fs->sb.magic = VFS_MAGIC;
        fs->sb.size = VFS_TOTAL_BLOCKS * VFS_BLOCK_SIZE;
        fs->sb.inode_count = 1;
        fs->sb.free_blocks = VFS_TOTAL_BLOCKS;
        fs->sb.data_start = sizeof(vfs_superblock);
        memset(fs->sb.block_map, 0, sizeof(fs->sb.block_map));
        fwrite(&fs->sb, sizeof(vfs_superblock), 1, fs->backing_file);
    } else {
        fread(&fs->sb, sizeof(vfs_superblock), 1, fs->backing_file);
        if (fs->sb.magic != VFS_MAGIC) {
            fclose(fs->backing_file);
            free(fs);
            return NULL;
        }
    }

    fs->inodes = malloc(fs->sb.inode_count * sizeof(vfs_inode));
    if (!fs->inodes) {
        fclose(fs->backing_file);
        free(fs);
        return NULL;
    }

    if (fs->sb.inode_count == 1 && fs->backing_file->_flags & _IO_WRITE) {
        fs->inodes[0].inode = 1;
        strcpy(fs->inodes[0].name, "/");
        fs->inodes[0].type = VFS_TYPE_DIR;
        fs->inodes[0].mode = 0755;
        fs->inodes[0].uid = 0;
        fs->inodes[0].gid = 0;
        fs->inodes[0].atime = fs->inodes[0].mtime = fs->inodes[0].ctime = time(NULL);
        fs->inodes[0].size = 0;
        fs->inodes[0].block_index = 0;
        fs->inodes[0].parent_inode = 0;
        fs->inodes[0].child_count = 0;
        fseek(fs->backing_file, sizeof(vfs_superblock), SEEK_SET);
        fwrite(fs->inodes, sizeof(vfs_inode), 1, fs->backing_file);
    } else {
        fseek(fs->backing_file, sizeof(vfs_superblock), SEEK_SET);
        fread(fs->inodes, sizeof(vfs_inode), fs->sb.inode_count, fs->backing_file);
    }

    return fs;
}

int vfs_unmount(VFS_FS* fs) {
    if (!fs) return -1;
    fseek(fs->backing_file, 0, SEEK_SET);
    fwrite(&fs->sb, sizeof(vfs_superblock), 1, fs->backing_file);
    fwrite(fs->inodes, sizeof(vfs_inode), fs->sb.inode_count, fs->backing_file);
    fclose(fs->backing_file);
    free(fs->inodes);
    free(fs);
    return 0;
}

VFS_FILE* vfs_fopen(VFS_FS* fs, const char* path, const char* mode) {
    if (!fs || !path || !mode) return NULL;

    vfs_inode* inode = find_inode_by_path(fs, path);
    if (!inode && (mode[0] == 'w' || mode[0] == 'a')) {
        char* parent_path = strdup(path);
        char* filename = strrchr(parent_path, '/');
        if (filename) {
            *filename = '\0';
            filename++;
        } else {
            filename = parent_path;
            parent_path = "/";
        }
        vfs_inode* parent = find_inode_by_path(fs, parent_path);
        if (!parent || parent->type != VFS_TYPE_DIR) {
            free(parent_path);
            return NULL;
        }

        uint32_t block_index = allocate_block(fs);
        if (block_index == UINT32_MAX) {
            free(parent_path);
            return NULL;
        }

        if (resize_inodes(fs) < 0) {
            free(parent_path);
            return NULL;
        }

        inode = &fs->inodes[fs->sb.inode_count++];
        inode->inode = fs->sb.inode_count;
        strncpy(inode->name, filename, VFS_MAX_NAME);
        inode->type = VFS_TYPE_FILE;
        inode->mode = 0666;
        inode->uid = 0;
        inode->gid = 0;
        inode->atime = inode->mtime = inode->ctime = time(NULL);
        inode->size = 0;
        inode->block_index = block_index;
        fs->sb.block_map[block_index] = inode->inode;
        inode->parent_inode = parent->inode;
        inode->child_count = 0;
        parent->child_count++;
        free(parent_path);
    }

    if (!inode) return NULL;

    VFS_FILE* file = malloc(sizeof(VFS_FILE));
    file->fs = fs;
    file->inode = inode;
    file->offset = (mode[0] == 'a') ? inode->size : 0;
    strncpy(file->mode, mode, 3);
    inode->atime = time(NULL);
    return file;
}

int vfs_fclose(VFS_FILE* file) {
    if (!file) return -1;
    file->inode->mtime = time(NULL);
    free(file);
    return 0;
}

size_t vfs_fread(void* ptr, size_t size, size_t nmemb, VFS_FILE* file) {
    if (!file || file->mode[0] != 'r') return 0;
    uint64_t to_read = size * nmemb;
    if (file->offset + to_read > file->inode->size) {
        to_read = file->inode->size - file->offset;
    }
    uint64_t block_offset = file->fs->sb.data_start + (file->inode->block_index * VFS_BLOCK_SIZE);
    fseek(file->fs->backing_file, block_offset + file->offset, SEEK_SET);
    size_t read = fread(ptr, 1, to_read, file->fs->backing_file);
    file->offset += read;
    file->inode->atime = time(NULL);
    return read / size;
}

size_t vfs_fwrite(const void* ptr, size_t size, size_t nmemb, VFS_FILE* file) {
    if (!file || (file->mode[0] != 'w' && file->mode[0] != 'a')) return 0;
    uint64_t to_write = size * nmemb;
    if (file->offset + to_write > VFS_BLOCK_SIZE) {
        to_write = VFS_BLOCK_SIZE - file->offset;
    }
    uint64_t block_offset = file->fs->sb.data_start + (file->inode->block_index * VFS_BLOCK_SIZE);
    fseek(file->fs->backing_file, block_offset + file->offset, SEEK_SET);
    size_t written = fwrite(ptr, 1, to_write, file->fs->backing_file);
    file->offset += written;
    if (file->offset > file->inode->size) {
        file->inode->size = file->offset;
    }
    file->inode->mtime = time(NULL);
    return written / size;
}

int vfs_fseek(VFS_FILE* file, long offset, int whence) {
    if (!file) return -1;
    uint64_t new_offset;
    switch (whence) {
        case SEEK_SET: new_offset = offset; break;
        case SEEK_CUR: new_offset = file->offset + offset; break;
        case SEEK_END: new_offset = file->inode->size + offset; break;
        default: return -1;
    }
    if (new_offset > file->inode->size || new_offset > VFS_BLOCK_SIZE) return -1;
    file->offset = new_offset;
    return 0;
}

long vfs_ftell(VFS_FILE* file) {
    if (!file) return -1;
    return file->offset;
}

int vfs_mkdir(VFS_FS* fs, const char* path, uint16_t mode) {
    if (!fs || !path) return -1;

    char* parent_path = strdup(path);
    char* dirname = strrchr(parent_path, '/');
    if (dirname) {
        *dirname = '\0';
        dirname++;
    } else {
        dirname = parent_path;
        parent_path = "/";
    }

    vfs_inode* parent = find_inode_by_path(fs, parent_path);
    if (!parent || parent->type != VFS_TYPE_DIR) {
        free(parent_path);
        return -1;
    }

    for (uint32_t i = 0; i < fs->sb.inode_count; i++) {
        if (fs->inodes[i].parent_inode == parent->inode &&
            strcmp(fs->inodes[i].name, dirname) == 0) {
            free(parent_path);
            return -1;
        }
    }

    if (resize_inodes(fs) < 0) {
        free(parent_path);
        return -1;
    }

    vfs_inode* inode = &fs->inodes[fs->sb.inode_count++];
    inode->inode = fs->sb.inode_count;
    strncpy(inode->name, dirname, VFS_MAX_NAME);
    inode->type = VFS_TYPE_DIR;
    inode->mode = mode;
    inode->uid = 0;
    inode->gid = 0;
    inode->atime = inode->mtime = inode->ctime = time(NULL);
    inode->size = 0;
    inode->block_index = 0;
    inode->parent_inode = parent->inode;
    inode->child_count = 0;
    parent->child_count++;
    free(parent_path);
    return 0;
}

int vfs_remove(VFS_FS* fs, const char* path) {
    if (!fs || !path) return -1;

    vfs_inode* inode = find_inode_by_path(fs, path);
    if (!inode || inode->child_count > 0) return -1;

    vfs_inode* parent = &fs->inodes[inode->parent_inode - 1];
    parent->child_count--;

    if (inode->type == VFS_TYPE_FILE) {
        free_block(fs, inode->block_index);
    }

    uint32_t i;
    for (i = 0; i < fs->sb.inode_count; i++) {
        if (fs->inodes[i].inode == inode->inode) break;
    }
    memmove(&fs->inodes[i], &fs->inodes[i + 1],
            (fs->sb.inode_count - i - 1) * sizeof(vfs_inode));
    fs->sb.inode_count--;
    return 0;
}

int vfs_rename(VFS_FS* fs, const char* old_path, const char* new_path) {
    if (!fs || !old_path || !new_path) return -1;

    vfs_inode* inode = find_inode_by_path(fs, old_path);
    if (!inode) return -1;

    char* new_parent_path = strdup(new_path);
    char* new_name = strrchr(new_parent_path, '/');
    if (new_name) {
        *new_name = '\0';
        new_name++;
    } else {
        new_name = new_parent_path;
        new_parent_path = "/";
    }

    vfs_inode* new_parent = find_inode_by_path(fs, new_parent_path);
    if (!new_parent || new_parent->type != VFS_TYPE_DIR) {
        free(new_parent_path);
        return -1;
    }

    vfs_inode* old_parent = &fs->inodes[inode->parent_inode - 1];
    old_parent->child_count--;
    new_parent->child_count++;

    strncpy(inode->name, new_name, VFS_MAX_NAME);
    inode->parent_inode = new_parent->inode;
    inode->mtime = time(NULL);

    free(new_parent_path);
    return 0;
}

VFS_DIR* vfs_opendir(VFS_FS* fs, const char* path) {
    if (!fs || !path) return NULL;

    vfs_inode* dir_inode = find_inode_by_path(fs, path);
    if (!dir_inode || dir_inode->type != VFS_TYPE_DIR) return NULL;

    VFS_DIR* dir = malloc(sizeof(VFS_DIR));
    dir->fs = fs;
    dir->dir_inode = dir_inode;
    dir->current_child = 0;
    dir_inode->atime = time(NULL);
    return dir;
}

int vfs_closedir(VFS_DIR* dir) {
    if (!dir) return -1;
    free(dir);
    return 0;
}

struct vfs_dirent* vfs_readdir(VFS_DIR* dir) {
    static struct vfs_dirent entry;
    struct vfs_dirent* result;
    if (vfs_readdir_r(dir, &entry, &result) == 0 && result) {
        return &entry;
    }
    return NULL;
}

int vfs_readdir_r(VFS_DIR* dir, struct vfs_dirent* entry, struct vfs_dirent** result) {
    if (!dir || !entry || !result) return -1;

    uint32_t count = 0;
    for (uint32_t i = 0; i < dir->fs->sb.inode_count; i++) {
        if (dir->fs->inodes[i].parent_inode == dir->dir_inode->inode) {
            if (count == dir->current_child) {
                entry->d_ino = dir->fs->inodes[i].inode;
                strncpy(entry->d_name, dir->fs->inodes[i].name, VFS_MAX_NAME);
                entry->d_type = dir->fs->inodes[i].type;
                dir->current_child++;
                *result = entry;
                return 0;
            }
            count++;
        }
    }
    *result = NULL;
    return 0;
}

void vfs_rewinddir(VFS_DIR* dir) {
    if (dir) dir->current_child = 0;
}
