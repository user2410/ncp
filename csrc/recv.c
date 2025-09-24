#define _POSIX_C_SOURCE 200809L  // For strdup and other POSIX functions
#include "recv.h"
#include "protocol.h"
#include "diskspace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define BUFFER_SIZE 8192
static char error_buffer[256] = {0};

struct Socket {
    int fd;
};

Socket* socket_new(int fd) {
    Socket* sock = malloc(sizeof(Socket));
    if (!sock) {
        snprintf(error_buffer, sizeof(error_buffer), "Memory allocation failed");
        return NULL;
    }
    sock->fd = fd;
    return sock;
}

void socket_free(Socket* sock) {
    if (!sock) return;
#ifdef _WIN32
    closesocket(sock->fd);
#else
    close(sock->fd);
#endif
    free(sock);
}

int socket_write(Socket* sock, const void* data, size_t size) {
    if (!sock || !data) {
        errno = EINVAL;
        return -1;
    }

    const char* ptr = data;
    size_t sent = 0;
    while (sent < size) {
        ssize_t result = send(sock->fd, ptr + sent, size - sent, 0);
        if (result <= 0) {
            snprintf(error_buffer, sizeof(error_buffer), "Send failed: %s", strerror(errno));
            return -1;
        }
        sent += result;
    }
    return 0;
}

int socket_read(Socket* sock, void* data, size_t size) {
    if (!sock || !data) {
        errno = EINVAL;
        return -1;
    }

    char* ptr = data;
    size_t received = 0;
    while (received < size) {
        ssize_t result = recv(sock->fd, ptr + received, size - received, 0);
        if (result <= 0) {
            snprintf(error_buffer, sizeof(error_buffer), "Receive failed: %s", strerror(errno));
            return -1;
        }
        received += result;
    }
    return 0;
}

static char* determine_final_path(const char* dst_path, const char* file_name, int is_dir) {
    struct stat st;
    
    // Check if destination exists
    if (stat(dst_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            // Destination is a directory, append file_name
            size_t dst_len = strlen(dst_path);
            size_t name_len = strlen(file_name);
            char* final_path = malloc(dst_len + name_len + 2); // +2 for '/' and '\0'
            if (!final_path) return NULL;
            
            strcpy(final_path, dst_path);
            if (dst_path[dst_len - 1] != '/') {
                strcat(final_path, "/");
            }
            strcat(final_path, file_name);
            return final_path;
        } else {
            // Destination is a file
            if (is_dir) {
                snprintf(error_buffer, sizeof(error_buffer), "Cannot receive directory to existing file");
                return NULL;
            }
            return strdup(dst_path);
        }
    } else {
        // Destination doesn't exist
        return strdup(dst_path);
    }
}

static int prompt_overwrite(const char* path) {
    printf("File %s already exists. Overwrite? (y/N): ", path);
    fflush(stdout);
    
    char input[10];
    if (!fgets(input, sizeof(input), stdin)) {
        return 0;
    }
    
    // Convert to lowercase
    for (char* p = input; *p; ++p) {
        *p = tolower(*p);
    }
    
    return strncmp(input, "y", 1) == 0 || strncmp(input, "yes", 3) == 0;
}

static int create_parent_directories(const char* path) {
    char* path_copy = strdup(path);
    if (!path_copy) return -1;
    
    char* last_slash = strrchr(path_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        
        // Create all parent directories with mode 0755
        char* p = path_copy;
        while (*p) {
            if (*p == '/') {
                *p = '\0';
                if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                    free(path_copy);
                    return -1;
                }
                *p = '/';
            }
            p++;
        }
        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            free(path_copy);
            return -1;
        }
    }
    
    free(path_copy);
    return 0;
}

static int handle_directory_entry(Socket* sock, const char* final_path, OverwriteMode overwrite_mode) {
    fprintf(stderr, "DEBUG: handle_directory_entry: path=%s\n", final_path);
    
    int fd_dup = dup(sock->fd);
    if (fd_dup < 0) {
        fprintf(stderr, "DEBUG: Failed to duplicate socket fd: %s\n", strerror(errno));
        return -1;
    }
    
    FILE* stream = fdopen(fd_dup, "wb+");
    if (!stream) {
        fprintf(stderr, "DEBUG: Failed to create stream: %s\n", strerror(errno));
        close(fd_dup);
        return -1;
    }
    
    // Disable buffering on socket stream
    if (setvbuf(stream, NULL, _IONBF, 0) != 0) {
        fprintf(stderr, "Warning: Failed to disable buffering: %s\n", strerror(errno));
    }

    struct stat st;
    if (stat(final_path, &st) == 0) {
        fprintf(stderr, "DEBUG: Path exists, mode=%o\n", st.st_mode & S_IFMT);
        if (!S_ISDIR(st.st_mode)) {
            // Path exists but is not a directory
            fprintf(stderr, "DEBUG: Path exists but is not a directory\n");
            switch (overwrite_mode) {
                case OVERWRITE_ASK:
                    if (!prompt_overwrite(final_path)) {
                        PreflightFail preflight_fail = {"User declined directory overwrite"};
                        write_preflight_fail(stream, &preflight_fail);
                        fclose(stream);
                        return -1;
                    }
                    break;
                case OVERWRITE_NO:
                case OVERWRITE_YES:
                    break;
            }
            
            // Remove existing non-directory
            if (unlink(final_path) != 0) {
                snprintf(error_buffer, sizeof(error_buffer), "Failed to remove existing file: %s", strerror(errno));
                fprintf(stderr, "DEBUG: %s\n", error_buffer);
                fclose(stream);
                return -1;
            }
            
            // Create the directory after removing file
            if (mkdir(final_path, 0755) != 0) {
                snprintf(error_buffer, sizeof(error_buffer), "Failed to create directory: %s", strerror(errno));
                fprintf(stderr, "DEBUG: %s\n", error_buffer);
                fclose(stream);
                return -1;
            }
        } else {
            fprintf(stderr, "DEBUG: Directory already exists, continuing\n");
        }
    } else {
        fprintf(stderr, "DEBUG: Creating directory and parents\n");
        // Create all parent directories
        if (create_parent_directories(final_path) != 0) {
            snprintf(error_buffer, sizeof(error_buffer), "Failed to create parent directories: %s", strerror(errno));
            fprintf(stderr, "DEBUG: %s\n", error_buffer);
            fclose(stream);
            return -1;
        }
        
        // Create the final directory
        if (mkdir(final_path, 0755) != 0) {
            snprintf(error_buffer, sizeof(error_buffer), "Failed to create directory: %s", strerror(errno));
            fprintf(stderr, "DEBUG: %s\n", error_buffer);
            fclose(stream);
            return -1;
        }
        fprintf(stderr, "DEBUG: Successfully created directory\n");
    }
    
    // Send success response for both new and existing directories
    fprintf(stderr, "DEBUG: Sending PreflightOK\n");
    PreflightOk preflight_ok = {0};
    if (write_preflight_ok(stream, &preflight_ok) < 0) {
        fprintf(stderr, "DEBUG: Failed to write PreflightOK\n");
        fclose(stream);
        return -1;
    }
    fflush(stream);  // Make sure PreflightOK is sent immediately
    
    fprintf(stderr, "DEBUG: Sending TransferResult\n");
    TransferResult transfer_result = {1, 0};  // Success, no bytes transferred for directories
    if (write_transfer_result(stream, &transfer_result) < 0) {
        fprintf(stderr, "DEBUG: Failed to write TransferResult\n");
        fclose(stream);
        return -1;
    }
    fflush(stream);  // Make sure TransferResult is sent immediately
    
    fprintf(stderr, "DEBUG: Directory entry handled successfully\n");
    fclose(stream);
    return 0;
}

static int handle_file_entry(Socket* sock, const char* final_path, 
                           const FileMeta* file_meta, OverwriteMode overwrite_mode) {
    FILE* stream = fdopen(dup(sock->fd), "wb+");
    if (!stream) return -1;
    
    struct stat st;
    if (stat(final_path, &st) == 0) {
        switch (overwrite_mode) {
            case OVERWRITE_ASK:
                if (!prompt_overwrite(final_path)) {
                    PreflightFail preflight_fail = {"User declined overwrite"};
                    write_preflight_fail(stream, &preflight_fail);
                    fclose(stream);
                    return -1;
                }
                break;
            case OVERWRITE_NO: {
                PreflightFail preflight_fail = {"File exists, skipping"};
                write_preflight_fail(stream, &preflight_fail);
                fclose(stream);
                return -1;
            }
            case OVERWRITE_YES:
                break;
        }
    }
    
    // Create parent directory
    if (create_parent_directories(final_path) != 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Failed to create parent directories: %s", strerror(errno));
        fclose(stream);
        return -1;
    }
    
    // Check disk space
    uint64_t available_space = get_available_space(final_path);
    int has_enough_space = check_disk_space(final_path, file_meta->size);
    
    if (!has_enough_space) {
        char available_str[32], needed_str[32];
        format_bytes(available_space, available_str, sizeof(available_str));
        format_bytes(file_meta->size, needed_str, sizeof(needed_str));
        
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Insufficient disk space. Need: %s, Available: %s",
                needed_str, available_str);
        
        PreflightFail preflight_fail = {error_msg};
        write_preflight_fail(stream, &preflight_fail);
        fclose(stream);
        return -1;
    }
    
    PreflightOk preflight_ok = {available_space};
    write_preflight_ok(stream, &preflight_ok);
    
    // Read transfer start
    uint8_t msg_type;
    if (read_message_type(stream, &msg_type) != 0 || msg_type != MSG_TRANSFER_START) {
        snprintf(error_buffer, sizeof(error_buffer), "Expected TransferStart message");
        fclose(stream);
        return -1;
    }
    
    uint32_t msg_len;
    read_message_length(stream, &msg_len);
    
    TransferStart transfer_start;
    read_transfer_start(stream, &transfer_start);
    
    // Create temporary file
    size_t path_len = strlen(final_path);
    char* temp_path = malloc(path_len + 10); // +10 for ".ncp_temp\0"
    if (!temp_path) {
        fclose(stream);
        return -1;
    }
    
    sprintf(temp_path, "%s.ncp_temp", final_path);
    
    FILE* temp_file = fopen(temp_path, "wb");
    if (!temp_file) {
        snprintf(error_buffer, sizeof(error_buffer), "Cannot create temporary file: %s", strerror(errno));
        free(temp_path);
        fclose(stream);
        return -1;
    }
    
    // Receive file data
    uint64_t total_bytes = 0;
    uint8_t buffer[BUFFER_SIZE];
    uint64_t file_size = transfer_start.file_size;
    
    while (total_bytes < file_size) {
        uint64_t remaining = file_size - total_bytes;
        size_t to_read = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
        
        if (read_exact_bytes(stream, buffer, to_read) != 0) {
            fclose(temp_file);
            unlink(temp_path);
            free(temp_path);
            fclose(stream);
            return -1;
        }
        
        if (fwrite(buffer, 1, to_read, temp_file) != to_read) {
            fclose(temp_file);
            unlink(temp_path);
            free(temp_path);
            fclose(stream);
            return -1;
        }
        
        total_bytes += to_read;
        
        if (total_bytes % (1024 * 1024) == 0 || total_bytes == file_size) {
            printf("\rReceived: %lu/%lu bytes", 
                   (unsigned long)total_bytes, (unsigned long)file_size);
            fflush(stdout);
        }
    }
    printf("\n");
    
    fclose(temp_file);
    
    // Rename temporary file to final path
    if (rename(temp_path, final_path) != 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Failed to rename temporary file: %s", strerror(errno));
        unlink(temp_path);
        free(temp_path);
        fclose(stream);
        return -1;
    }
    
    free(temp_path);
    
    TransferResult transfer_result = {1, total_bytes};
    write_transfer_result(stream, &transfer_result);
    
    fclose(stream);
    return 0;
}

static int handle_connection(Socket* sock, const char* dst_path, OverwriteMode overwrite_mode) {
    FILE* stream = fdopen(dup(sock->fd), "rb+");
    if (!stream) return -1;
    
    while (1) {
        uint8_t msg_type;
        if (read_message_type(stream, &msg_type) != 0) {
            if (feof(stream)) break;
            fclose(stream);
            return -1;
        }
        
        if (msg_type != MSG_META) {
            snprintf(error_buffer, sizeof(error_buffer), "Expected Meta message");
            fclose(stream);
            return -1;
        }
        
        uint32_t msg_len;
        read_message_length(stream, &msg_len);
        
        FileMeta file_meta;
        if (read_meta(stream, &file_meta) != 0) {
            fclose(stream);
            return -1;
        }
        
        char* final_path = determine_final_path(dst_path, file_meta.name, file_meta.is_dir);
        if (!final_path) {
            file_meta_free(&file_meta);
            fclose(stream);
            return -1;
        }
        
        printf("Receiving %s: %s (%lu bytes) to %s\n",
               file_meta.is_dir ? "directory" : "file",
               file_meta.name,
               (unsigned long)file_meta.size,
               final_path);
        
        int result;
        if (file_meta.is_dir) {
            result = handle_directory_entry(sock, final_path, file_meta.overwrite_mode);
        } else {
            result = handle_file_entry(sock, final_path, &file_meta, file_meta.overwrite_mode);
        }
        
        free(final_path);
        file_meta_free(&file_meta);
        
        if (result != 0) {
            fclose(stream);
            return -1;
        }
    }
    
    fclose(stream);
    return 0;
}

int recv_execute(const char* host, uint16_t port, 
                const char* dst_path, OverwriteMode overwrite_mode) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Socket creation failed: %s", strerror(errno));
        return -1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &address.sin_addr) <= 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Invalid address: %s", strerror(errno));
#ifdef _WIN32
        closesocket(server_fd);
#else
        close(server_fd);
#endif
        return -1;
    }
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Bind failed: %s", strerror(errno));
#ifdef _WIN32
        closesocket(server_fd);
#else
        close(server_fd);
#endif
        return -1;
    }
    
    if (listen(server_fd, 1) < 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Listen failed: %s", strerror(errno));
#ifdef _WIN32
        closesocket(server_fd);
#else
        close(server_fd);
#endif
        return -1;
    }
    
    printf("Listening on port %d\n", port);
    
    struct sockaddr_in client_addr = {0};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    
    if (client_fd < 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Accept failed: %s", strerror(errno));
#ifdef _WIN32
        closesocket(server_fd);
#else
        close(server_fd);
#endif
        return -1;
    }
    
    printf("Connection from: %s:%d\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));
    
    Socket* client_socket = socket_new(client_fd);
    if (!client_socket) {
#ifdef _WIN32
        closesocket(client_fd);
        closesocket(server_fd);
#else
        close(client_fd);
        close(server_fd);
#endif
        return -1;
    }
    
    int result = handle_connection(client_socket, dst_path, overwrite_mode);
    
    socket_free(client_socket);
#ifdef _WIN32
    closesocket(server_fd);
    WSACleanup();
#else
    close(server_fd);
#endif
    
    if (result == 0) {
        printf("Transfer completed successfully\n");
    }
    
    return result;
}

int recv_execute_connect(const char* host, uint16_t port,
                        const char* dst_path, OverwriteMode overwrite_mode) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Socket creation failed: %s", strerror(errno));
        return -1;
    }
    
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Invalid address: %s", strerror(errno));
#ifdef _WIN32
        closesocket(client_fd);
#else
        close(client_fd);
#endif
        return -1;
    }
    
    printf("Connecting to %s:%d...\n", host, port);
    
    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Connection failed: %s", strerror(errno));
#ifdef _WIN32
        closesocket(client_fd);
#else
        close(client_fd);
#endif
        return -1;
    }
    
    printf("Connection established\n");
    
    Socket* client_socket = socket_new(client_fd);
    if (!client_socket) {
#ifdef _WIN32
        closesocket(client_fd);
#else
        close(client_fd);
#endif
        return -1;
    }
    
    int result = handle_connection(client_socket, dst_path, overwrite_mode);
    
    socket_free(client_socket);
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    if (result == 0) {
        printf("Transfer completed successfully\n");
    }
    
    return result;
}

const char* recv_get_error(void) {
    return error_buffer[0] ? error_buffer : "Unknown error";
}
