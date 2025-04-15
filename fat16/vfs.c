#include "vfs.h"
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>

#define FLASH_SIZE (512 * 1024)
#define SECTOR_SIZE 512
#define CLUSTER_SIZE (4 * 1024)
#define NUM_CLUSTERS 125
#define ROOT_ENTRIES 256
#define FAT_SIZE (NUM_CLUSTERS * 2)
#define DIR_ENTRIES (CLUSTER_SIZE / 32) // 128
#define MAX_DEPTH 4
#define HEADER_SIZE 512
#define MAX_OPEN_FILES 16
#define MAX_PATH 256

struct FlashHeader {
    uint32_t magic; // 0xF416
    uint32_t crc32;
    uint64_t sequence;
    uint8_t active;
    uint8_t reserved[HEADER_SIZE - 20];
};

struct BootSector {
    uint16_t bytesPerSector;
    uint8_t sectorsPerCluster;
    uint16_t reservedSectors;
    uint8_t numFATs;
    uint16_t rootEntryCount;
    uint32_t totalSectors;
};

struct DirEntry {
    char name[11];
    uint8_t attributes;
    uint8_t reserved[10];
    uint16_t time;      // FAT16 time (5/6/5 bits: hour/minute/second)
    uint16_t date;      // FAT16 date (7/4/5 bits: year/month/day)
    uint16_t firstCluster;
    uint32_t size;
};

struct FileDesc {
    bool used;
    char path[MAX_PATH];
    uint16_t firstCluster;
    uint32_t size;
    uint32_t offset;
    uint8_t mode;
};

struct DirHandle {
    bool used;
    uint16_t cluster; // 0 for root
    int index;
};

struct FAT16VFS {
    struct BootSector boot;
    uint16_t fat[NUM_CLUSTERS];
    struct DirEntry root[ROOT_ENTRIES];
    uint8_t data[NUM_CLUSTERS * CLUSTER_SIZE];
    bool dirty;
    uint8_t activeBuffer;
    uint64_t sequence;
    struct FileDesc openFiles[MAX_OPEN_FILES];
    struct DirHandle openDirs[MAX_OPEN_FILES];
    FILE* flash;
};

static struct FAT16VFS vfs;
static pthread_mutex_t vfs_mutex = PTHREAD_MUTEX_INITIALIZER;
int errno;

// Convert FAT16 date/time to Unix time_t (simplified, assumes local time)
static time_t fat16_to_unix_time(uint16_t date, uint16_t time) {
    struct tm tm = {0};
    tm.tm_year = ((date >> 9) & 0x7F) + 1980 - 1900; // Years since 1900
    tm.tm_mon = ((date >> 5) & 0x0F) - 1;            // 0-11
    tm.tm_mday = (date & 0x1F);                      // 1-31
    tm.tm_hour = (time >> 11) & 0x1F;                // 0-23
    tm.tm_min = (time >> 5) & 0x3F;                  // 0-59
    tm.tm_sec = (time & 0x1F) * 2;                   // 0-58 (2-second resolution)
    return mktime(&tm);
}

// CRC32 implementation (standard polynomial 0xEDB88320)
static uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    static uint32_t table[256];
    static bool initialized = false;

    if (!initialized) {
        for (int i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }

    for (size_t i = 0; i < size; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

int vfs_init(void) {
    pthread_mutex_lock(&vfs_mutex);

    vfs.boot.bytesPerSector = SECTOR_SIZE;
    vfs.boot.sectorsPerCluster = CLUSTER_SIZE / SECTOR_SIZE;
    vfs.boot.reservedSectors = 1;
    vfs.boot.numFATs = 1;
    vfs.boot.rootEntryCount = ROOT_ENTRIES;
    vfs.boot.totalSectors = FLASH_SIZE / SECTOR_SIZE;

    memset(vfs.fat, 0, sizeof(vfs.fat));
    memset(vfs.root, 0, sizeof(vfs.root));
    memset(vfs.data, 0, sizeof(vfs.data));
    vfs.dirty = false;
    vfs.activeBuffer = 0;
    vfs.sequence = 0;
    memset(vfs.openFiles, 0, sizeof(vfs.openFiles));
    memset(vfs.openDirs, 0, sizeof(vfs.openDirs));
    vfs.flash = NULL;

    pthread_mutex_unlock(&vfs_mutex);
    return 0;
}

int vfs_mount(FILE* flash) {
    pthread_mutex_lock(&vfs_mutex);

    vfs.flash = flash;
    struct FlashHeader headers[2];
    uint8_t buffer[FLASH_SIZE];

    for (int i = 0; i < 2; i++) {
        fseek(flash, i * FLASH_SIZE, SEEK_SET);
        fread(&headers[i], 1, HEADER_SIZE, flash);
    }

    int selected = -1;
    if (headers[0].magic == 0xF416 && headers[1].magic == 0xF416) {
        selected = headers[0].sequence > headers[1].sequence ? 0 : 1;
    } else if (headers[0].magic == 0xF416) {
        selected = 0;
    } else if (headers[1].magic == 0xF416) {
        selected = 1;
    } else {
        vfs_init();
        pthread_mutex_unlock(&vfs_mutex);
        return 0; // Fresh filesystem
    }

    fseek(flash, selected * FLASH_SIZE, SEEK_SET);
    fread(buffer, 1, FLASH_SIZE, flash);

    uint32_t crc = crc32(&buffer[HEADER_SIZE], FLASH_SIZE - HEADER_SIZE);
    if (crc != headers[selected].crc32) {
        selected = selected ? 0 : 1;
        fseek(flash, selected * FLASH_SIZE, SEEK_SET);
        fread(buffer, 1, FLASH_SIZE, flash);
        crc = crc32(&buffer[HEADER_SIZE], FLASH_SIZE - HEADER_SIZE);
        if (crc != headers[selected].crc32) {
            vfs_init();
            pthread_mutex_unlock(&vfs_mutex);
            return 0; // Both corrupt, start fresh
        }
    }

    uint32_t offset = HEADER_SIZE;
    memcpy(&vfs.boot, &buffer[offset], SECTOR_SIZE);
    offset += SECTOR_SIZE;
    memcpy(vfs.fat, &buffer[offset], FAT_SIZE);
    offset += SECTOR_SIZE;
    memcpy(&vfs.root, &buffer[offset], ROOT_ENTRIES * sizeof(struct DirEntry));
    offset += ROOT_ENTRIES * sizeof(struct DirEntry);
    memcpy(&vfs.data, &buffer[offset], NUM_CLUSTERS * CLUSTER_SIZE);

    vfs.activeBuffer = selected;
    vfs.sequence = headers[selected].sequence;
    vfs.dirty = false;

    pthread_mutex_unlock(&vfs_mutex);
    return 0;
}

int vfs_flush(void) {
    pthread_mutex_lock(&vfs_mutex);

    if (!vfs.dirty || !vfs.flash) {
        pthread_mutex_unlock(&vfs_mutex);
        return 0;
    }

    uint8_t buffer[FLASH_SIZE];
    uint32_t offset = HEADER_SIZE;
    memcpy(&buffer[offset], &vfs.boot, SECTOR_SIZE);
    offset += SECTOR_SIZE;
    memcpy(&buffer[offset], vfs.fat, FAT_SIZE);
    offset += SECTOR_SIZE;
    memcpy(&buffer[offset], vfs.root, ROOT_ENTRIES * sizeof(struct DirEntry));
    offset += ROOT_ENTRIES * sizeof(struct DirEntry);
    memcpy(&buffer[offset], vfs.data, NUM_CLUSTERS * CLUSTER_SIZE);

    uint32_t crc = crc32(&buffer[HEADER_SIZE], FLASH_SIZE - HEADER_SIZE);

    uint8_t inactiveBuffer = vfs.activeBuffer ? 0 : 1;
    fseek(vfs.flash, inactiveBuffer * FLASH_SIZE, SEEK_SET);

    struct FlashHeader header = {
        .magic = 0xF416,
        .crc32 = crc,
        .sequence = ++vfs.sequence,
        .active = 1
    };
    memcpy(buffer, &header, HEADER_SIZE);

    if (fwrite(buffer, 1, FLASH_SIZE, vfs.flash) != FLASH_SIZE) {
        errno = EIO;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }
    fflush(vfs.flash);

    uint8_t verify[FLASH_SIZE];
    fseek(vfs.flash, inactiveBuffer * FLASH_SIZE, SEEK_SET);
    fread(verify, 1, FLASH_SIZE, vfs.flash);
    uint32_t verifyCRC = crc32(&verify[HEADER_SIZE], FLASH_SIZE - HEADER_SIZE);
    if (verifyCRC != crc) {
        errno = EIO;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    vfs.activeBuffer = inactiveBuffer;
    vfs.dirty = false;

    pthread_mutex_unlock(&vfs_mutex);
    return 0;
}

static struct DirEntry* findEntry(const char* path, uint16_t* clusterOut, bool parent) {
    if (!path || path[0] != '/') return NULL;
    char components[MAX_DEPTH][12];
    int depth = 0;

    char* p = (char*)path + 1;
    while (*p && depth < MAX_DEPTH) {
        char* next = strchr(p, '/');
        if (!next) next = p + strlen(p);
        if (next - p > 11) return NULL;
        strncpy(components[depth], p, next - p);
        components[depth][next - p] = '\0';
        depth++;
        p = next + 1;
        if (*p == '\0') break;
    }
    if (depth > MAX_DEPTH) return NULL;
    if (parent && depth > 0) depth--;

    struct DirEntry* currentDir = vfs.root;
    uint16_t currentCluster = 0;
    int entriesPerDir = ROOT_ENTRIES;

    for (int i = 0; i < depth; i++) {
        bool found = false;
        for (int j = 0; j < entriesPerDir; j++) {
            if (currentDir[j].name[0] == 0 || currentDir[j].name[0] == 0xE5) continue;
            if (strncmp(currentDir[j].name, components[i], 11) == 0 &&
                (currentDir[j].attributes & 0x10)) {
                currentCluster = currentDir[j].firstCluster;
                currentDir = (struct DirEntry*)&vfs.data[(currentCluster - 2) * CLUSTER_SIZE];
                entriesPerDir = DIR_ENTRIES;
                found = true;
                break;
            }
        }
        if (!found) return NULL;
    }

    if (!parent) {
        for (int j = 0; j < entriesPerDir; j++) {
            if (currentDir[j].name[0] == 0 || currentDir[j].name[0] == 0xE5) continue;
            if (strncmp(currentDir[j].name, components[depth - 1], 11) == 0) {
                *clusterOut = currentCluster;
                return ¤tDir[j];
            }
        }
        return NULL;
    }

    *clusterOut = currentCluster;
    return currentDir;
}

int vfs_open(const char* path, int mode) {
    pthread_mutex_lock(&vfs_mutex);

    bool create = (mode & O_CREAT);
    bool read = (mode & O_RDONLY) || (mode & O_RDWR);
    bool write = (mode & O_WRONLY) || (mode & O_RDWR);

    if (!read && !write) {
        errno = EINVAL;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    uint16_t cluster;
    struct DirEntry* entry = findEntry(path, &cluster);
    if (!entry && !create) {
        errno = ENOENT;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    if (entry && create && (mode & O_EXCL)) {
        errno = EEXIST;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    if (!entry) {
        uint16_t parentCluster;
        struct DirEntry* parentDir = findEntry(path, &parentCluster, true);
        if (!parentDir) {
            errno = ENOENT;
            pthread_mutex_unlock(&vfs_mutex);
            return -1;
        }

        char* name = strrchr(path, '/') + 1;
        if (strlen(name) > 11) {
            errno = ENAMETOOLONG;
            pthread_mutex_unlock(&vfs_mutex);
            return -1;
        }
        int entriesPerDir = parentCluster ? DIR_ENTRIES : ROOT_ENTRIES;

        for (int i = 0; i < entriesPerDir; i++) {
            if (strncmp(parentDir[i].name, name, 11) == 0 &&
                parentDir[i].name[0] != 0 && parentDir[i].name[0] != 0xE5) {
                errno = EEXIST;
                pthread_mutex_unlock(&vfs_mutex);
                return -1;
            }
        }

        int freeEntry = -1;
        for (int i = 0; i < entriesPerDir; i++) {
            if (parentDir[i].name[0] == 0 || parentDir[i].name[0] == 0xE5) {
                freeEntry = i;
                break;
            }
        }
        if (freeEntry == -1) {
            errno = ENOSPC;
            pthread_mutex_unlock(&vfs_mutex);
            return -1;
        }

        uint16_t firstCluster = 0;
        for (int i = 2; i < NUM_CLUSTERS; i++) {
            if (vfs.fat[i] == 0) {
                vfs.fat[i] = 0xFFFF;
                firstCluster = i;
                break;
            }
        }
        if (firstCluster == 0) {
            errno = ENOSPC;
            pthread_mutex_unlock(&vfs_mutex);
            return -1;
        }

        strncpy(parentDir[freeEntry].name, name, 11);
        parentDir[freeEntry].attributes = 0x20;
        parentDir[freeEntry].size = 0;
        parentDir[freeEntry].firstCluster = firstCluster;
        vfs.dirty = true;
        entry = &parentDir[freeEntry];
    }

    if (entry->attributes & 0x10) {
        errno = EISDIR;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!vfs.openFiles[i].used) {
            fd = i;
            break;
        }
    }
    if (fd == -1) {
        errno = EMFILE;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    vfs.openFiles[fd].used = true;
    strncpy(vfs.openFiles[fd].path, path, MAX_PATH);
    vfs.openFiles[fd].firstCluster = entry->firstCluster;
    vfs.openFiles[fd].size = entry->size;
    vfs.openFiles[fd].offset = 0;
    vfs.openFiles[fd].mode = (read && write) ? 3 : (read ? 1 : 2);

    pthread_mutex_unlock(&vfs_mutex);
    return fd;
}

int vfs_close(int fd) {
    pthread_mutex_lock(&vfs_mutex);

    if (fd < 0 || fd >= MAX_OPEN_FILES || !vfs.openFiles[fd].used) {
        errno = EBADF;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }
    vfs.openFiles[fd].used = false;

    pthread_mutex_unlock(&vfs_mutex);
    return 0;
}

ssize_t vfs_read(int fd, void* buf, size_t count) {
    pthread_mutex_lock(&vfs_mutex);

    if (fd < 0 || fd >= MAX_OPEN_FILES || !vfs.openFiles[fd].used ||
        !(vfs.openFiles[fd].mode & 1)) {
        errno = EBADF;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    struct FileDesc* file = &vfs.openFiles[fd];
    if (file->offset >= file->size) {
        pthread_mutex_unlock(&vfs_mutex);
        return 0;
    }

    uint32_t bytesRead = 0;
    uint16_t current = file->firstCluster;
    uint32_t clusterOffset = file->offset / CLUSTER_SIZE;
    uint32_t intraOffset = file->offset % CLUSTER_SIZE;

    for (uint32_t i = 0; i < clusterOffset && current != 0xFFFF; i++) {
        current = vfs.fat[current];
    }
    if (current == 0xFFFF) {
        errno = EIO;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    while (bytesRead < count && file->offset < file->size) {
        uint32_t chunkSize = CLUSTER_SIZE - intraOffset;
        if (chunkSize > count - bytesRead) chunkSize = count - bytesRead;
        if (chunkSize > file->size - file->offset) chunkSize = file->size - file->offset;
        memcpy((uint8_t*)buf + bytesRead,
               &vfs.data[(current - 2) * CLUSTER_SIZE + intraOffset],
               chunkSize);
        bytesRead += chunkSize;
        file->offset += chunkSize;
        intraOffset += chunkSize;

        if (intraOffset >= CLUSTER_SIZE) {
            current = vfs.fat[current];
            intraOffset = 0;
            if (current == 0xFFFF) break;
        }
    }

    pthread_mutex_unlock(&vfs_mutex);
    return bytesRead;
}

ssize_t vfs_write(int fd, const void* buf, size_t count) {
    pthread_mutex_lock(&vfs_mutex);

    if (fd < 0 || fd >= MAX_OPEN_FILES || !vfs.openFiles[fd].used ||
        !(vfs.openFiles[fd].mode & 2)) {
        errno = EBADF;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    struct FileDesc* file = &vfs.openFiles[fd];
    uint32_t newSize = file->offset + count;
    int clustersNeeded = (newSize + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
    int clustersCurrent = (file->size + CLUSTER_SIZE - 1) / CLUSTER_SIZE;

    if (clustersNeeded > clustersCurrent) {
        uint16_t lastCluster = file->firstCluster;
        int clustersToAdd = clustersNeeded - clustersCurrent;

        if (lastCluster == 0) {
            for (int i = 2; i < NUM_CLUSTERS; i++) {
                if (vfs.fat[i] == 0) {
                    vfs.fat[i] = 0xFFFF;
                    file->firstCluster = i;
                    lastCluster = i;
                    clustersToAdd--;
                    break;
                }
            }
        }

        while (clustersToAdd > 0) {
            uint16_t newCluster = 0;
            for (int i = 2; i < NUM_CLUSTERS; i++) {
                if (vfs.fat[i] == 0) {
                    vfs.fat[i] = 0xFFFF;
                    newCluster = i;
                    break;
                }
            }
            if (newCluster == 0) {
                errno = ENOSPC;
                pthread_mutex_unlock(&vfs_mutex);
                return -1;
            }

            while (vfs.fat[lastCluster] != 0xFFFF) {
                lastCluster = vfs.fat[lastCluster];
            }
            vfs.fat[lastCluster] = newCluster;
            lastCluster = newCluster;
            clustersToAdd--;
        }
    }

    uint32_t bytesWritten = 0;
    uint16_t current = file->firstCluster;
    uint32_t clusterOffset = file->offset / CLUSTER_SIZE;
    uint32_t intraOffset = file->offset % CLUSTER_SIZE;

    for (uint32_t i = 0; i < clusterOffset && current != 0xFFFF; i++) {
        current = vfs.fat[current];
    }
    if (current == 0xFFFF) {
        errno = EIO;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    while (bytesWritten < count) {
        uint32_t chunkSize = CLUSTER_SIZE - intraOffset;
        if (chunkSize > count - bytesWritten) chunkSize = count - bytesWritten;
        memcpy(&vfs.data[(current - 2) * CLUSTER_SIZE + intraOffset],
               (uint8_t*)buf + bytesWritten,
               chunkSize);
        bytesWritten += chunkSize;
        file->offset += chunkSize;
        intraOffset += chunkSize;

        if (intraOffset >= CLUSTER_SIZE) {
            current = vfs.fat[current];
            intraOffset = 0;
            if (current == 0xFFFF) break;
        }
    }

    if (file->offset > file->size) {
        file->size = file->offset;
        uint16_t cluster;
        struct DirEntry* entry = findEntry(file->path, &cluster, false);
        if (entry) {
            entry->size = file->size;
            // Update timestamp (simplified: set to current time if available)
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            if (tm) {
                entry->date = ((tm->tm_year + 1900 - 1980) << 9) |
                              ((tm->tm_mon + 1) << 5) | tm->tm_mday;
                entry->time = (tm->tm_hour << 11) | (tm->tm_min << 5) |
                              (tm->tm_sec / 2);
            }
        }
    }

    vfs.dirty = true;

    pthread_mutex_unlock(&vfs_mutex);
    return bytesWritten;
}

int vfs_unlink(const char* path) {
    pthread_mutex_lock(&vfs_mutex);

    uint16_t cluster;
    struct DirEntry* entry = findEntry(path, &cluster);
    if (!entry) {
        errno = ENOENT;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }
    if (entry->attributes & 0x10) {
        errno = EISDIR;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    uint16_t current = entry->firstCluster;
    while (current != 0xFFFF) {
        uint16_t next = vfs.fat[current];
        vfs.fat[current] = 0;
        current = next;
    }

    entry->name[0] = 0xE5;
    vfs.dirty = true;

    pthread_mutex_unlock(&vfs_mutex);
    return 0;
}

DIR* vfs_opendir(const char* path) {
    pthread_mutex_lock(&vfs_mutex);

    uint16_t cluster;
    struct DirEntry* entry = findEntry(path, &cluster);
    if (!entry || !(entry->attributes & 0x10)) {
        if (strcmp(path, "/") == 0) {
            cluster = 0;
        } else {
            errno = ENOTDIR;
            pthread_mutex_unlock(&vfs_mutex);
            return NULL;
        }
    }

    int handle = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!vfs.openDirs[i].used) {
            handle = i;
            break;
        }
    }
    if (handle == -1) {
        errno = EMFILE;
        pthread_mutex_unlock(&vfs_mutex);
        return NULL;
    }

    vfs.openDirs[handle].used = true;
    vfs.openDirs[handle].cluster = cluster;
    vfs.openDirs[handle].index = 2; // Skip . and ..

    pthread_mutex_unlock(&vfs_mutex);
    return (DIR*)&vfs.openDirs[handle];
}

struct dirent* vfs_readdir(DIR* dirp) {
    pthread_mutex_lock(&vfs_mutex);

    struct DirHandle* handle = (struct DirHandle*)dirp;
    if (!handle || !handle->used) {
        errno = EBADF;
        pthread_mutex_unlock(&vfs_mutex);
        return NULL;
    }

    struct DirEntry* dir = handle->cluster ?
        (struct DirEntry*)&vfs.data[(handle->cluster - 2) * CLUSTER_SIZE] : vfs.root;
    int entriesPerDir = handle->cluster ? DIR_ENTRIES : ROOT_ENTRIES;

    while (handle->index < entriesPerDir) {
        if (dir[handle->index].name[0] != 0 && dir[handle->index].name[0] != 0xE5) {
            static struct dirent entry;
            strncpy(entry.d_name, dir[handle->index].name, 11);
            entry.d_name[11] = '\0';
            entry.d_type = (dir[handle->index].attributes & 0x10) ? DT_DIR : DT_REG;
            handle->index++;
            pthread_mutex_unlock(&vfs_mutex);
            return &entry;
        }
        handle->index++;
    }

    pthread_mutex_unlock(&vfs_mutex);
    return NULL;
}

int vfs_closedir(DIR* dirp) {
    pthread_mutex_lock(&vfs_mutex);

    struct DirHandle* handle = (struct DirHandle*)dirp;
    if (!handle || !handle->used) {
        errno = EBADF;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }
    handle->used = false;

    pthread_mutex_unlock(&vfs_mutex);
    return 0;
}

int vfs_mkdir(const char* path, uint32_t mode) {
    pthread_mutex_lock(&vfs_mutex);

    uint16_t parentCluster;
    struct DirEntry* parentDir = findEntry(path, &parentCluster, true);
    if (!parentDir) {
        errno = ENOENT;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    char* name = strrchr(path, '/') + 1;
    if (strlen(name) > 11) {
        errno = ENAMETOOLONG;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }
    int entriesPerDir = parentCluster ? DIR_ENTRIES : ROOT_ENTRIES;

    for (int i = 0; i < entriesPerDir; i++) {
        if (strncmp(parentDir[i].name, name, 11) == 0 &&
            parentDir[i].name[0] != 0 && parentDir[i].name[0] != 0xE5) {
            errno = EEXIST;
            pthread_mutex_unlock(&vfs_mutex);
            return -1;
        }
    }

    int freeEntry = -1;
    for (int i = 0; i < entriesPerDir; i++) {
        if (parentDir[i].name[0] == 0 || parentDir[i].name[0] == 0xE5) {
            freeEntry = i;
            break;
        }
    }
    if (freeEntry == -1) {
        errno = ENOSPC;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    uint16_t firstCluster = 0;
    for (int i = 2; i < NUM_CLUSTERS; i++) {
        if (vfs.fat[i] == 0) {
            vfs.fat[i] = 0xFFFF;
            firstCluster = i;
            break;
        }
    }
    if (firstCluster == 0) {
        errno = ENOSPC;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    strncpy(parentDir[freeEntry].name, name, 11);
    parentDir[freeEntry].attributes = 0x10;
    parentDir[freeEntry].size = 0;
    parentDir[freeEntry].firstCluster = firstCluster;

    struct DirEntry* newDir = (struct DirEntry*)&vfs.data[(firstCluster - 2) * CLUSTER_SIZE];
    memset(newDir, 0, CLUSTER_SIZE);
    strncpy(newDir[0].name, ".          ", 11);
    newDir[0].attributes = 0x10;
    newDir[0].firstCluster = firstCluster;
    strncpy(newDir[1].name, "..         ", 11);
    newDir[1].attributes = 0x10;
    newDir[1].firstCluster = parentCluster;

    // Set timestamps for new directory
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    if (tm) {
        parentDir[freeEntry].date = ((tm->tm_year + 1900 - 1980) << 9) |
                                    ((tm->tm_mon + 1) << 5) | tm->tm_mday;
        parentDir[freeEntry].time = (tm->tm_hour << 11) | (tm->tm_min << 5) |
                                    (tm->tm_sec / 2);
        newDir[0].date = newDir[1].date = parentDir[freeEntry].date;
        newDir[0].time = newDir[1].time = parentDir[freeEntry].time;
    }

    vfs.dirty = true;

    pthread_mutex_unlock(&vfs_mutex);
    return 0;
}

int vfs_rmdir(const char* path) {
    pthread_mutex_lock(&vfs_mutex);

    if (strcmp(path, "/") == 0) {
        errno = EBUSY;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    uint16_t cluster;
    struct DirEntry* entry = findEntry(path, &cluster);
    if (!entry) {
        errno = ENOENT;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }
    if (!(entry->attributes & 0x10)) {
        errno = ENOTDIR;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    struct DirEntry* dir = cluster ?
        (struct DirEntry*)&vfs.data[(cluster - 2) * CLUSTER_SIZE] : vfs.root;
    int entriesPerDir = cluster ? DIR_ENTRIES : ROOT_ENTRIES;
    for (int i = 2; i < entriesPerDir; i++) {
        if (dir[i].name[0] != 0 && dir[i].name[0] != 0xE5) {
            errno = ENOTEMPTY;
            pthread_mutex_unlock(&vfs_mutex);
            return -1;
        }
    }

    uint16_t current = entry->firstCluster;
    while (current != 0xFFFF) {
        uint16_t next = vfs.fat[current];
        vfs.fat[current] = 0;
        current = next;
    }

    entry->name[0] = 0xE5;
    vfs.dirty = true;

    pthread_mutex_unlock(&vfs_mutex);
    return 0;
}

int vfs_stat(const char* path, struct stat* statbuf) {
    pthread_mutex_lock(&vfs_mutex);

    if (!path || !statbuf) {
        errno = EINVAL;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    uint16_t cluster;
    struct DirEntry* entry = findEntry(path, &cluster);
    if (!entry) {
        if (strcmp(path, "/") == 0) {
            // Special case for root
            statbuf->st_mode = S_IFDIR;
            statbuf->st_size = 0; // Root size is not tracked
            // Use a default timestamp or system time if no RTC
            time_t now = time(NULL);
            statbuf->st_mtime = now != (time_t)-1 ? now : 0;
            pthread_mutex_unlock(&vfs_mutex);
            return 0;
        }
        errno = ENOENT;
        pthread_mutex_unlock(&vfs_mutex);
        return -1;
    }

    statbuf->st_mode = (entry->attributes & 0x10) ? S_IFDIR : S_IFREG;
    statbuf->st_size = entry->size;
    statbuf->st_mtime = fat16_to_unix_time(entry->date, entry->time);

    pthread_mutex_unlock(&vfs_mutex);
    return 0;
}
