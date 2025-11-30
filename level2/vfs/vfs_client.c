#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "vfs_client.h"

#define SERVER_PORT 12345
#define SERVER_ADDR "127.0.0.1"

struct vfs_file {
    uint32_t fd_id;
    int sock;
    uint64_t offset; // Local offset tracking
};

struct vfs_dir {
    uint32_t dir_id;
    int sock;
};

static int connect_to_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = { .sin_family = AF_INET, .sin_port = htons(SERVER_PORT) };
    inet_pton(AF_INET, SERVER_ADDR, &server_addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

VFS_FILE* vfs_fopen(const char* path, const char* mode) {
    int sock = connect_to_server();
    if (sock < 0) return NULL;

    uint32_t mode_len = strlen(mode) + 1;
    uint32_t path_len = strlen(path) + 1;
    uint32_t payload_len = 8 + path_len;
    char* req = malloc(5 + payload_len);
    req[0] = 1;
    *(uint32_t*)(req + 1) = payload_len;
    *(uint32_t*)(req + 5) = mode_len;
    strcpy(req + 9, mode);
    strcpy(req + 9 + mode_len, path);

    send(sock, req, 5 + payload_len, 0);
    free(req);

    char resp[1024];
    int bytes = recv(sock, resp, sizeof(resp), 0);
    if (bytes <= 0 || resp[0] != 0) {
        close(sock);
        return NULL;
    }

    uint32_t fd_id = *(uint32_t*)(resp + 5);
    VFS_FILE* file = malloc(sizeof(VFS_FILE));
    file->fd_id = fd_id;
    file->sock = sock;
    file->offset = 0;
    return file;
}

int vfs_fclose(VFS_FILE* file) {
    if (!file) return -1;

    char req[9];
    req[0] = 2;
    *(uint32_t*)(req + 1) = 4;
    *(uint32_t*)(req + 5) = file->fd_id;
    send(file->sock, req, 9, 0);

    char resp[9];
    recv(file->sock, resp, 9, 0);
    int result = *(int*)(resp + 5);
    close(file->sock);
    free(file);
    return result;
}

size_t vfs_fread(void* ptr, size_t size, size_t nmemb, VFS_FILE* file) {
    if (!file) return 0;

    char req[17];
    req[0] = 3;
    *(uint32_t*)(req + 1) = 12;
    *(uint32_t*)(req + 5) = file->fd_id;
    *(uint32_t*)(req + 9) = size;
    *(uint32_t*)(req + 13) = nmemb;
    send(file->sock, req, 17, 0);

    char resp[5];
    recv(file->sock, resp, 5, 0);
    if (resp[0] != 0) return 0;

    uint32_t data_len = *(uint32_t*)(resp + 1);
    char* data = malloc(data_len);
    recv(file->sock, data, data_len, 0);
    memcpy(ptr, data, data_len);
    size_t read = data_len / size;
    file->offset += data_len;
    free(data);
    return read;
}

size_t vfs_fwrite(const void* ptr, size_t size, size_t nmemb, VFS_FILE* file) {
    if (!file) return 0;

    uint32_t payload_len = 12 + size * nmemb;
    char* req = malloc(5 + payload_len);
    req[0] = 4;
    *(uint32_t*)(req + 1) = payload_len;
    *(uint32_t*)(req + 5) = file->fd_id;
    *(uint32_t*)(req + 9) = size;
    *(uint32_t*)(req + 13) = nmemb;
    memcpy(req + 17, ptr, size * nmemb);

    send(file->sock, req, 5 + payload_len, 0);
    free(req);

    char resp[9];
    recv(file->sock, resp, 9, 0);
    if (resp[0] != 0) return 0;
    size_t written = *(uint32_t*)(resp + 5);
    file->offset += written * size;
    return written;
}

int vfs_fseek(VFS_FILE* file, long offset, int whence) {
    if (!file) return -1;

    char req[17];
    req[0] = 5;
    *(uint32_t*)(req + 1) = 12;
    *(uint32_t*)(req + 5) = file->fd_id;
    *(long*)(req + 9) = offset;
    *(int*)(req + 13) = whence;
    send(file->sock, req, 17, 0);

    char resp[9];
    recv(file->sock, resp, 9, 0);
    if (resp[0] != 0) return -1;
    file->offset = offset; // Simplified; server maintains true offset
    return *(int*)(resp + 5);
}

long vfs_ftell(VFS_FILE* file) {
    if (!file) return -1;

    char req[9];
    req[0] = 6;
    *(uint32_t*)(req + 1) = 4;
    *(uint32_t*)(req + 5) = file->fd_id;
    send(file->sock, req, 9, 0);

    char resp[9];
    recv(file->sock, resp, 9, 0);
    if (resp[0] != 0) return -1;
    return *(long*)(resp + 5);
}

int vfs_mkdir(const char* path, uint16_t mode) {
    int sock = connect_to_server();
    if (sock < 0) return -1;

    uint32_t path_len = strlen(path) + 1;
    uint32_t payload_len = 2 + path_len;
    char* req = malloc(5 + payload_len);
    req[0] = 7;
    *(uint32_t*)(req + 1) = payload_len;
    *(uint16_t*)(req + 5) = mode;
    strcpy(req + 7, path);

    send(sock, req, 5 + payload_len, 0);
    free(req);

    char resp[9];
    recv(sock, resp, 9, 0);
    int result = *(int*)(resp + 5);
    close(sock);
    return result;
}

int vfs_remove(const char* path) {
    int sock = connect_to_server();
    if (sock < 0) return -1;

    uint32_t path_len = strlen(path) + 1;
    char* req = malloc(5 + path_len);
    req[0] = 8;
    *(uint32_t*)(req + 1) = path_len;
    strcpy(req + 5, path);

    send(sock, req, 5 + path_len, 0);
    free(req);

    char resp[9];
    recv(sock, resp, 9, 0);
    int result = *(int*)(resp + 5);
    close(sock);
    return result;
}

int vfs_rename(const char* old_path, const char* new_path) {
    int sock = connect_to_server();
    if (sock < 0) return -1;

    uint32_t old_path_len = strlen(old_path) + 1;
    uint32_t new_path_len = strlen(new_path) + 1;
    uint32_t payload_len = 4 + old_path_len + new_path_len;
    char* req = malloc(5 + payload_len);
    req[0] = 9;
    *(uint32_t*)(req + 1) = payload_len;
    *(uint32_t*)(req + 5) = old_path_len;
    strcpy(req + 9, old_path);
    strcpy(req + 9 + old_path_len, new_path);

    send(sock, req, 5 + payload_len, 0);
    free(req);

    char resp[9];
    recv(sock, resp, 9, 0);
    int result = *(int*)(resp + 5);
    close(sock);
    return result;
}

VFS_DIR* vfs_opendir(const char* path) {
    int sock = connect_to_server();
    if (sock < 0) return NULL;

    uint32_t path_len = strlen(path) + 1;
    char* req = malloc(5 + path_len);
    req[0] = 10;
    *(uint32_t*)(req + 1) = path_len;
    strcpy(req + 5, path);

    send(sock, req, 5 + path_len, 0);
    free(req);

    char resp[1024];
    int bytes = recv(sock, resp, sizeof(resp), 0);
    if (bytes <= 0 || resp[0] != 0) {
        close(sock);
        return NULL;
    }

    uint32_t dir_id = *(uint32_t*)(resp + 5);
    VFS_DIR* dir = malloc(sizeof(VFS_DIR));
    dir->dir_id = dir_id;
    dir->sock = sock;
    return dir;
}

int vfs_closedir(VFS_DIR* dir) {
    if (!dir) return -1;

    char req[9];
    req[0] = 11;
    *(uint32_t*)(req + 1) = 4;
    *(uint32_t*)(req + 5) = dir->dir_id;
    send(dir->sock, req, 9, 0);

    char resp[9];
    recv(dir->sock, resp, 9, 0);
    int result = *(int*)(resp + 5);
    close(dir->sock);
    free(dir);
    return result;
}

vfs_dirent* vfs_readdir(VFS_DIR* dir) {
    if (!dir) return NULL;

    char req[9];
    req[0] = 12;
    *(uint32_t*)(req + 1) = 4;
    *(uint32_t*)(req + 5) = dir->dir_id;
    send(dir->sock, req, 9, 0);

    char resp[5];
    recv(dir->sock, resp, 5, 0);
    if (*(uint32_t*)(resp + 1) == 0) return NULL;

    uint32_t data_len = *(uint32_t*)(resp + 1);
    char* data = malloc(data_len);
    recv(dir->sock, data, data_len, 0);

    static vfs_dirent entry;
    entry.d_ino = *(uint32_t*)data;
    strncpy(entry.d_name, data + 4, VFS_MAX_NAME);
    entry.d_type = data[4 + VFS_MAX_NAME];
    free(data);
    return &entry;
}

int vfs_readdir_r(VFS_DIR* dir, vfs_dirent* entry, vfs_dirent** result) {
    vfs_dirent* e = vfs_readdir(dir);
    if (e) {
        *entry = *e;
        *result = entry;
    } else {
        *result = NULL;
    }
    return 0;
}

void vfs_rewinddir(VFS_DIR* dir) {
    if (!dir) return;

    char req[9];
    req[0] = 13;
    *(uint32_t*)(req + 1) = 4;
    *(uint32_t*)(req + 5) = dir->dir_id;
    send(dir->sock, req, 9, 0);

    char resp[5];
    recv(dir->sock, resp, 5, 0);
}
