#include "send.h"
#include "protocol.h"
#include "directory.h"
#include "recv.h"
#include "socket_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>

#define BUFFER_SIZE 8192

static int send_file_content(Socket* sock, const char* path, uint64_t size) {
    // If size is 0, this is a directory, no content to send
    if (size == 0) {
        return 0;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open file: %s\n", strerror(errno));
        return -1;
    }

    uint8_t buffer[BUFFER_SIZE];
    uint64_t total_sent = 0;
    ssize_t bytes_read;

    while (total_sent < size) {
        bytes_read = read(fd, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) {
            if (bytes_read < 0) {
                fprintf(stderr, "Failed to read file: %s\n", strerror(errno));
            } else {
                fprintf(stderr, "Unexpected end of file\n");
            }
            close(fd);
            return -1;
        }

        if (socket_write(sock, buffer, bytes_read) < 0) {
            fprintf(stderr, "Failed to send data: %s\n", strerror(errno));
            close(fd);
            return -1;
        }

        total_sent += bytes_read;
    }

    close(fd);
    return 0;
}

static int send_file(Socket* sock, const char* path, OverwriteMode overwrite_mode) {
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        fprintf(stderr, "Failed to stat file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    // Get base name for standalone files
    char* base_name = strdup(path);
    if (!base_name) {
        fprintf(stderr, "Failed to allocate memory for basename\n");
        return -1;
    }
    char* last_slash = strrchr(base_name, '/');
    char* entry_name = strdup(last_slash ? last_slash + 1 : base_name);
    free(base_name);
    
    if (!entry_name) {
        fprintf(stderr, "Failed to allocate memory for entry name\n");
        return -1;
    }

    // Send file metadata
    FileMeta meta = {
        .name = entry_name,  // FileMeta takes ownership of entry_name
        .size = path_stat.st_size,
        .is_dir = S_ISDIR(path_stat.st_mode),
        .overwrite_mode = (uint8_t)overwrite_mode
    };

    // Duplicate the file descriptor for the FILE* stream
    int fd_dup = dup(sock->fd);
    if (fd_dup < 0) {
        fprintf(stderr, "Failed to duplicate socket descriptor: %s\n", strerror(errno));
        return -1;
    }

    FILE* sock_file = fdopen(fd_dup, "w+b");  // Use w+b for bidirectional buffered I/O
    if (!sock_file) {
        fprintf(stderr, "Failed to create socket stream: %s\n", strerror(errno));
        close(fd_dup);
        return -1;
    }

    if (write_meta(sock_file, &meta) < 0) {
        fprintf(stderr, "Failed to send metadata\n");
        fclose(sock_file);
        return -1;
    }
    fflush(sock_file);

    // Wait for preflight response
    uint8_t msg_type;
    if (read_message_type(sock_file, &msg_type) < 0) {
        fprintf(stderr, "Failed to read preflight response\n");
        fclose(sock_file);
        return -1;
    }

    // Handle preflight failure
    if (msg_type == MSG_PREFLIGHT_FAIL) {
        PreflightFail fail;
        if (read_preflight_fail(sock_file, &fail) == 0) {
            fprintf(stderr, "Transfer rejected: %s\n", fail.reason);
            preflight_fail_free(&fail);
        } else {
            fprintf(stderr, "Failed to read preflight failure details\n");
        }
        fclose(sock_file);
        return -1;
    }

    // Verify we got preflight OK
    if (msg_type != MSG_PREFLIGHT_OK) {
        fprintf(stderr, "Unexpected preflight response: %d\n", msg_type);
        fclose(sock_file);
        return -1;
    }

    if (!meta.is_dir) {
        // Only send transfer start and content for regular files
        TransferStart start = { .file_size = path_stat.st_size };
        if (write_transfer_start(sock_file, &start) < 0) {
            fprintf(stderr, "Failed to send transfer start\n");
            fclose(sock_file);
            return -1;
        }
        fflush(sock_file);

        // Send file content
        if (send_file_content(sock, path, path_stat.st_size) < 0) {
            fprintf(stderr, "Failed to send file content\n");
            fclose(sock_file);
            return -1;
        }
    }

    // Wait for transfer result
    TransferResult result;
    if (read_transfer_result(sock_file, &result) < 0) {
        fprintf(stderr, "Failed to read transfer result\n");
        fclose(sock_file);
        return -1;
    }

    // FileMeta takes ownership of entry_name
    fclose(sock_file);
    return result.ok ? 0 : -1;
}

static int send_directory_entry(Socket* sock, const FileEntry* entry, OverwriteMode overwrite_mode) {
    fprintf(stderr, "DEBUG: Sending entry: path=%s, is_dir=%d\n", 
            entry->relative_path, entry->is_dir);
            
    // Send file metadata first
    FileMeta meta = {
        .name = strdup(entry->relative_path),
        .size = entry->size,
        .is_dir = entry->is_dir,
        .overwrite_mode = (uint8_t)overwrite_mode
    };

    if (!meta.name) {
        fprintf(stderr, "Failed to allocate memory for entry name\n");
        return -1;
    }

    // Duplicate the file descriptor for the FILE* stream
    int fd_dup = dup(sock->fd);
    if (fd_dup < 0) {
        fprintf(stderr, "Failed to duplicate socket descriptor: %s\n", strerror(errno));
        free(meta.name);
        return -1;
    }

    FILE* sock_file = fdopen(fd_dup, "w+b");  // Use w+b for bidirectional buffered I/O
    if (!sock_file) {
        fprintf(stderr, "Failed to create socket stream: %s\n", strerror(errno));
        free(meta.name);
        close(fd_dup);
        return -1;
    }

    // Send meta message and wait for preflight response
    if (write_meta(sock_file, &meta) < 0) {
        fprintf(stderr, "Failed to send metadata\n");
        free(meta.name);
        fclose(sock_file);
        return -1;
    }
    fflush(sock_file);

    // Wait for preflight response
    uint8_t msg_type;
    uint32_t msg_len;
    if (read_message_type(sock_file, &msg_type) < 0) {
        fprintf(stderr, "DEBUG: Failed to read preflight response type\n");
        free(meta.name);
        fclose(sock_file);
        return -1;
    }
    if (read_message_length(sock_file, &msg_len) < 0) {
        fprintf(stderr, "DEBUG: Failed to read preflight response length\n");
        free(meta.name);
        fclose(sock_file);
        return -1;
    }
    fprintf(stderr, "DEBUG: Got preflight response type: %d, length: %u\n", msg_type, msg_len);

    // Handle preflight failure
    if (msg_type == MSG_PREFLIGHT_FAIL) {
        fprintf(stderr, "DEBUG: Handling preflight fail message\n");
        PreflightFail fail;
        memset(&fail, 0, sizeof(fail));
        if (read_preflight_fail(sock_file, &fail) == 0) {
            fprintf(stderr, "Transfer rejected: %s\n", fail.reason ? fail.reason : "No reason given");
            preflight_fail_free(&fail);
        } else {
            fprintf(stderr, "DEBUG: Failed to read preflight failure details, errno=%d (%s)\n", 
                    errno, strerror(errno));
        }
        free(meta.name);
        fclose(sock_file);
        return -1;
    }

    // Verify we got preflight OK
    if (msg_type != MSG_PREFLIGHT_OK) {
        fprintf(stderr, "Unexpected preflight response: %d\n", msg_type);
        free(meta.name);
        fclose(sock_file);
        return -1;
    }

    // For files (not directories), send content
    if (!meta.is_dir) {
        // Send transfer start message
        TransferStart start = { .file_size = entry->size };
        if (write_transfer_start(sock_file, &start) < 0) {
            fprintf(stderr, "Failed to send transfer start\n");
            free(meta.name);
            fclose(sock_file);
            return -1;
        }
        fflush(sock_file);

        // Send actual file content
        if (send_file_content(sock, entry->path, entry->size) < 0) {
            fprintf(stderr, "Failed to send file content\n");
            free(meta.name);
            fclose(sock_file);
            return -1;
        }
    }

    // For files, we wait for transfer result
    if (!meta.is_dir) {
        TransferResult result;
        if (read_transfer_result(sock_file, &result) < 0) {
            fprintf(stderr, "Failed to read transfer result\n");
            free(meta.name);
            fclose(sock_file);
            return -1;
        }
        if (!result.ok) {
            fprintf(stderr, "Transfer failed\n");
            free(meta.name);
            fclose(sock_file);
            return -1;
        }
    }

    free(meta.name);
    fclose(sock_file);
    return 0;
}

static int send_directory(Socket* sock, const char* dir_path, OverwriteMode overwrite_mode) {
    FileEntryArray* entries = walk_directory(dir_path);
    if (!entries) {
        fprintf(stderr, "Failed to walk directory: %s\n", dir_path);
        return -1;
    }

    printf("Directory contains %zu entries\n", entries->count);

    int ret = 0;
    // Process all entries, including the root directory
    // They'll be in the right order (root dir first, then subdirs, then files)
    for (size_t i = 0; i < entries->count; i++) {
        FileEntry* entry = &entries->entries[i];
        // Don't skip the root directory entry anymore
        
        printf("Transferring %s: %s\n", 
            entry->is_dir ? "directory" : "file",
            entry->relative_path);

        // Send each file/directory using its relative path
        if (send_directory_entry(sock, entry, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send '%s'\n", entry->path);
            ret = -1;
            break;
        }
    }

    file_entry_array_free(entries);
    return ret;
}

void ncp_execute_send(const char* host, uint16_t port,
                    const char* src_path, uint32_t retries,
                    OverwriteMode overwrite_mode) {
    struct stat st;
    if (stat(src_path, &st) != 0) {
        fprintf(stderr, "Error: Source path '%s' does not exist\n", src_path);
        exit(1);
    }

    // Get the base name for the source path
    char* base_name = strdup(src_path);
    if (!base_name) {
        fprintf(stderr, "Failed to allocate memory for basename\n");
        exit(1);
    }

    // Create socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Failed to create socket");
        free(base_name);
        exit(1);
    }

    // Setup server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address/Address not supported\n");
        close(sock_fd);
        exit(1);
    }

    // Connect with retry logic
    uint32_t retry_count = 0;
    while (retry_count <= retries) {
        if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
            break;
        }
        
        if (retry_count == retries) {
            perror("Connection failed");
            close(sock_fd);
            exit(1);
        }
        
        fprintf(stderr, "Connection attempt %d failed, retrying...\n", retry_count + 1);
        sleep(1);
        retry_count++;
    }

    Socket* sock = socket_new(sock_fd);
    if (!sock) {
        close(sock_fd);
        exit(1);
    }

    if (S_ISDIR(st.st_mode)) {
        // Send just the directory contents without the root directory
        if (send_directory(sock, src_path, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send: %s\n", src_path);
            socket_free(sock);
            free(base_name);
            exit(1);
        }
    } else {
        // For single files
        if (send_file(sock, src_path, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send: %s\n", src_path);
            socket_free(sock);
            free(base_name);
            exit(1);
        }
    }

    free(base_name);
    socket_free(sock);
}

void ncp_execute_send_listen(uint16_t port, const char* src_path,
                          OverwriteMode overwrite_mode) {
    struct stat st;
    if (stat(src_path, &st) != 0) {
        fprintf(stderr, "Error: Source path '%s' does not exist\n", src_path);
        exit(1);
    }

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Failed to create socket");
        exit(1);
    }

    // Setup server address
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        exit(1);
    }

    // Bind
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(1);
    }

    // Listen
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(1);
    }

    printf("Listening on port %d...\n", port);

    // Accept connection
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_sock < 0) {
        perror("Accept failed");
        close(server_fd);
        exit(1);
    }

    Socket* sock = socket_new(client_sock);
    if (!sock) {
        close(client_sock);
        close(server_fd);
        exit(1);
    }

    // Get the base name for the source path
    char* base_name = strdup(src_path);
    if (!base_name) {
        fprintf(stderr, "Failed to allocate memory for basename\n");
        socket_free(sock);
        close(server_fd);
        exit(1);
    }

    if (S_ISDIR(st.st_mode)) {
        printf("Sending directory: %s\n", src_path);
        // Send just the directory contents without the root directory
        if (send_directory(sock, src_path, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send: %s\n", src_path);
            socket_free(sock);
            free(base_name);
            close(server_fd);
            exit(1);
        }
    } else {
        printf("Sending file: %s\n", src_path);
        if (send_file(sock, src_path, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send: %s\n", src_path);
            socket_free(sock);
            free(base_name);
            close(server_fd);
            exit(1);
        }
    }

    free(base_name);
    socket_free(sock);
    close(server_fd);
}
