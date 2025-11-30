#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include "vfs.h"

#define PORT 12345
#define MAX_EVENTS 100
#define BUFFER_SIZE 4096

typedef struct {
    int sock;              // Client socket FD
    VFS_FS* fs;            // Filesystem instance
    char buffer[BUFFER_SIZE]; // Read buffer
    size_t buffer_pos;     // Current position in buffer
} client_state;

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    VFS_FS* fs = vfs_mount("vfs.dat");
    if (!fs) {
        perror("Failed to mount VFS");
        return 1;
    }

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    set_nonblocking(server_sock);
    struct sockaddr_in server_addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(PORT) };
    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, SOMAXCONN);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1 failed");
        return 1;
    }

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = server_sock;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_sock, &ev);

    client_state* clients = calloc(MAX_EVENTS, sizeof(client_state));
    int num_clients = 0;

    printf("VFS Server listening on port %d with epoll\n", PORT);

    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_sock) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
                if (client_sock < 0) continue;

                set_nonblocking(client_sock);
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_sock;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &ev);

                clients[num_clients].sock = client_sock;
                clients[num_clients].fs = fs;
                clients[num_clients].buffer_pos = 0;
                num_clients++;
            } else {
                int sock = events[i].data.fd;
                client_state* client = NULL;
                for (int j = 0; j < num_clients; j++) {
                    if (clients[j].sock == sock) {
                        client = &clients[j];
                        break;
                    }
                }
                if (!client) continue;

                ssize_t bytes_read = read(sock, client->buffer + client->buffer_pos,
                                         BUFFER_SIZE - client->buffer_pos);
                if (bytes_read <= 0) {
                    if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock, NULL);
                        close(sock);
                        for (int j = 0; j < num_clients; j++) {
                            if (clients[j].sock == sock) {
                                clients[j] = clients[--num_clients];
                                break;
                            }
                        }
                    }
                    continue;
                }

                client->buffer_pos += bytes_read;
                while (client->buffer_pos >= 5) {
                    uint8_t req_type = client->buffer[0];
                    uint32_t payload_len = *(uint32_t*)(client->buffer + 1);
                    if (client->buffer_pos < 5 + payload_len) break;

                    char* payload = client->buffer + 5;
                    uint8_t resp[BUFFER_SIZE];
                    resp[0] = 0; // Default success
                    uint32_t* resp_len = (uint32_t*)(resp + 1);

                    switch (req_type) {
                        case 1: { // fopen
                            uint32_t mode_len = *(uint32_t*)payload;
                            char* mode = payload + 4;
                            char* path = payload + 8;
                            VFS_FILE* file = vfs_fopen(fs, path, mode);
                            if (file) {
                                uint32_t fd_id = (uint32_t)(uintptr_t)file;
                                *resp_len = 4;
                                *(uint32_t*)(resp + 5) = fd_id;
                                write(sock, resp, 9);
                            } else {
                                const char* msg = "File open failed";
                                *resp_len = strlen(msg);
                                strcpy((char*)(resp + 5), msg);
                                resp[0] = 1;
                                write(sock, resp, 5 + *resp_len);
                            }
                            break;
                        }
                        case 2: { // fclose
                            uint32_t fd_id = *(uint32_t*)payload;
                            VFS_FILE* file = (VFS_FILE*)(uintptr_t)fd_id;
                            int result = vfs_fclose(file);
                            *resp_len = 4;
                            *(int*)(resp + 5) = result;
                            if (result < 0) resp[0] = 1;
                            write(sock, resp, 9);
                            break;
                        }
                        case 3: { // fread
                            uint32_t fd_id = *(uint32_t*)payload;
                            uint32_t size = *(uint32_t*)(payload + 4);
                            uint32_t nmemb = *(uint32_t*)(payload + 8);
                            VFS_FILE* file = (VFS_FILE*)(uintptr_t)fd_id;
                            char* data = malloc(size * nmemb);
                            size_t read = vfs_fread(data, size, nmemb, file);
                            *resp_len = read;
                            memcpy(resp + 5, data, read);
                            if (read == 0 && size * nmemb > 0) resp[0] = 1;
                            write(sock, resp, 5 + read);
                            free(data);
                            break;
                        }
                        case 4: { // fwrite
                            uint32_t fd_id = *(uint32_t*)payload;
                            uint32_t size = *(uint32_t*)(payload + 4);
                            uint32_t nmemb = *(uint32_t*)(payload + 8);
                            char* data = payload + 12;
                            VFS_FILE* file = (VFS_FILE*)(uintptr_t)fd_id;
                            size_t written = vfs_fwrite(data, size, nmemb, file);
                            *resp_len = 4;
                            *(uint32_t*)(resp + 5) = written;
                            if (written == 0 && size * nmemb > 0) resp[0] = 1;
                            write(sock, resp, 9);
                            break;
                        }
                        case 5: { // fseek
                            uint32_t fd_id = *(uint32_t*)payload;
                            long offset = *(long*)(payload + 4);
                            int whence = *(int*)(payload + 12);
                            VFS_FILE* file = (VFS_FILE*)(uintptr_t)fd_id;
                            int result = vfs_fseek(file, offset, whence);
                            *resp_len = 4;
                            *(int*)(resp + 5) = result;
                            if (result < 0) resp[0] = 1;
                            write(sock, resp, 9);
                            break;
                        }
                        case 6: { // ftell
                            uint32_t fd_id = *(uint32_t*)payload;
                            VFS_FILE* file = (VFS_FILE*)(uintptr_t)fd_id;
                            long pos = vfs_ftell(file);
                            *resp_len = 4;
                            *(long*)(resp + 5) = pos;
                            if (pos < 0) resp[0] = 1;
                            write(sock, resp, 9);
                            break;
                        }
                        case 7: { // mkdir
                            uint16_t mode = *(uint16_t*)payload;
                            char* path = payload + 2;
                            int result = vfs_mkdir(fs, path, mode);
                            *resp_len = 4;
                            *(int*)(resp + 5) = result;
                            if (result < 0) resp[0] = 1;
                            write(sock, resp, 9);
                            break;
                        }
                        case 8: { // remove
                            char* path = payload;
                            int result = vfs_remove(fs, path);
                            *resp_len = 4;
                            *(int*)(resp + 5) = result;
                            if (result < 0) resp[0] = 1;
                            write(sock, resp, 9);
                            break;
                        }
                        case 9: { // rename
                            uint32_t old_path_len = *(uint32_t*)payload;
                            char* old_path = payload + 4;
                            char* new_path = payload + 4 + old_path_len;
                            int result = vfs_rename(fs, old_path, new_path);
                            *resp_len = 4;
                            *(int*)(resp + 5) = result;
                            if (result < 0) resp[0] = 1;
                            write(sock, resp, 9);
                            break;
                        }
                        case 10: { // opendir
                            char* path = payload;
                            VFS_DIR* dir = vfs_opendir(fs, path);
                            if (dir) {
                                uint32_t dir_id = (uint32_t)(uintptr_t)dir;
                                *resp_len = 4;
                                *(uint32_t*)(resp + 5) = dir_id;
                                write(sock, resp, 9);
                            } else {
                                const char* msg = "Dir open failed";
                                *resp_len = strlen(msg);
                                strcpy((char*)(resp + 5), msg);
                                resp[0] = 1;
                                write(sock, resp, 5 + *resp_len);
                            }
                            break;
                        }
                        case 11: { // closedir
                            uint32_t dir_id = *(uint32_t*)payload;
                            VFS_DIR* dir = (VFS_DIR*)(uintptr_t)dir_id;
                            int result = vfs_closedir(dir);
                            *resp_len = 4;
                            *(int*)(resp + 5) = result;
                            if (result < 0) resp[0] = 1;
                            write(sock, resp, 9);
                            break;
                        }
                        case 12: { // readdir
                            uint32_t dir_id = *(uint32_t*)payload;
                            VFS_DIR* dir = (VFS_DIR*)(uintptr_t)dir_id;
                            vfs_dirent* entry = vfs_readdir(dir);
                            if (entry) {
                                *resp_len = 4 + VFS_MAX_NAME + 1;
                                *(uint32_t*)(resp + 5) = entry->d_ino;
                                strcpy((char*)(resp + 9), entry->d_name);
                                resp[9 + VFS_MAX_NAME] = entry->d_type;
                                write(sock, resp, 5 + *resp_len);
                            } else {
                                *resp_len = 0;
                                write(sock, resp, 5);
                            }
                            break;
                        }
                        case 13: { // rewinddir
                            uint32_t dir_id = *(uint32_t*)payload;
                            VFS_DIR* dir = (VFS_DIR*)(uintptr_t)dir_id;
                            vfs_rewinddir(dir);
                            *resp_len = 0;
                            write(sock, resp, 5);
                            break;
                        }
                    }

                    memmove(client->buffer, client->buffer + 5 + payload_len,
                            client->buffer_pos - (5 + payload_len));
                    client->buffer_pos -= 5 + payload_len;
                }
            }
        }
    }

    vfs_unmount(fs);
    close(server_sock);
    close(epoll_fd);
    free(clients);
    return 0;
}
