#define _POSIX_C_SOURCE 200809L  // For strdup and other POSIX functions
#include "send.h"
#include "protocol.h"
#include "directory.h"
#include "recv.h"
#include "socket_internal.h"
#include "logging.h"
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

    DEBUG_LOG(2, "DEBUG: Send: About to send metadata\n");
    if (write_meta(sock_file, &meta) < 0) {
        fprintf(stderr, "Failed to send metadata\n");
        fclose(sock_file);
        return -1;
    }
    DEBUG_LOG(2, "DEBUG: Send: Metadata sent, flushing\n");
    fflush(sock_file);
    DEBUG_LOG(2, "DEBUG: Send: Metadata flushed, waiting for preflight response\n");

    // Wait for preflight response
    uint8_t msg_type;
    uint32_t msg_len;
    DEBUG_LOG(2, "DEBUG: Send: About to read preflight response type\n");
    if (read_message_type(sock_file, &msg_type) < 0) {
        fprintf(stderr, "Failed to read preflight response\n");
        fclose(sock_file);
        return -1;
    }
    if (read_message_length(sock_file, &msg_len) < 0) {
        fprintf(stderr, "Failed to read preflight response length\n");
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

    // Read and consume the PreflightOK payload (available space)
    PreflightOk preflight_ok;
    if (read_preflight_ok(sock_file, &preflight_ok) < 0) {
        fprintf(stderr, "Failed to read preflight OK payload\n");
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

    // Wait for transfer result (sent for both files and directories)
    DEBUG_LOG(2, "DEBUG: Send: Waiting for transfer result\n");
    TransferResult result;
    if (read_transfer_result_full(sock_file, &result) < 0) {
        DEBUG_LOG(2, "DEBUG: Send: Failed to read transfer result\n");
        fclose(sock_file);
        return -1;
    }
    DEBUG_LOG(2, "DEBUG: Send: Transfer result received: ok=%d, bytes=%llu\n", 
            result.ok, (unsigned long long)result.received_bytes);

    // FileMeta takes ownership of entry_name
    fclose(sock_file);
    return result.ok ? 0 : -1;
}

static int send_directory_entry(FILE* sock_file, Socket* sock, const FileEntry* entry, OverwriteMode overwrite_mode) {
    DEBUG_LOG(2, "DEBUG: Sending entry: path=%s, is_dir=%d\n", 
            entry->relative_path, entry->is_dir);
            
    // Send file metadata first
    FileMeta meta = {
        .name = strdup(entry->relative_path),
        .size = entry->size,
        .is_dir = entry->is_dir,
        .overwrite_mode = (uint8_t)overwrite_mode
    };

    if (!meta.name) {
        LOG_ERROR("Failed to allocate memory for entry name\n");
        return -1;
    }

    // Send meta message and wait for preflight response
    if (write_meta(sock_file, &meta) < 0) {
        fprintf(stderr, "Failed to send metadata\n");
        free(meta.name);
        return -1;
    }
    fflush(sock_file);

    // Wait for preflight response
    uint8_t msg_type;
    uint32_t msg_len;
    if (read_message_type(sock_file, &msg_type) < 0) {
        DEBUG_LOG(2, "DEBUG: Failed to read preflight response type\n");
        free(meta.name);
        return -1;
    }
    if (read_message_length(sock_file, &msg_len) < 0) {
        DEBUG_LOG(2, "DEBUG: Failed to read preflight response length\n");
        free(meta.name);
        return -1;
    }
    DEBUG_LOG(2, "DEBUG: Got preflight response type: %d, length: %u\n", msg_type, msg_len);

    // Handle preflight failure
    if (msg_type == MSG_PREFLIGHT_FAIL) {
        DEBUG_LOG(2, "DEBUG: Handling preflight fail message\n");
        PreflightFail fail;
        memset(&fail, 0, sizeof(fail));
        if (read_preflight_fail(sock_file, &fail) == 0) {
            fprintf(stderr, "Transfer rejected: %s\n", fail.reason ? fail.reason : "No reason given");
            preflight_fail_free(&fail);
        } else {
            DEBUG_LOG(2, "DEBUG: Failed to read preflight failure details, errno=%d (%s)\n", 
                    errno, strerror(errno));
        }
        free(meta.name);
        return -1;
    }

    // Verify we got preflight OK
    if (msg_type != MSG_PREFLIGHT_OK) {
        fprintf(stderr, "Unexpected preflight response: %d\n", msg_type);
        free(meta.name);
        return -1;
    }

    // Read and consume the PreflightOK payload (available space)
    PreflightOk preflight_ok;
    if (read_preflight_ok(sock_file, &preflight_ok) < 0) {
        DEBUG_LOG(2, "DEBUG: Failed to read preflight OK payload\n");
        free(meta.name);
        return -1;
    }
    DEBUG_LOG(2, "DEBUG: PreflightOK received, available space: %llu\n", 
            (unsigned long long)preflight_ok.available_space);

    // For files (not directories), send content
    if (!meta.is_dir) {
        // Send transfer start message
        TransferStart start = { .file_size = entry->size };
        if (write_transfer_start(sock_file, &start) < 0) {
            fprintf(stderr, "Failed to send transfer start\n");
            free(meta.name);
            return -1;
        }
        fflush(sock_file);

        // Send actual file content
        if (send_file_content(sock, entry->path, entry->size) < 0) {
            fprintf(stderr, "Failed to send file content\n");
            free(meta.name);
            return -1;
        }
    }

    // Wait for transfer result (both files and directories send this)
    DEBUG_LOG(2, "DEBUG: Send: Waiting for transfer result\n");
    TransferResult result;
    if (read_transfer_result_full(sock_file, &result) < 0) {
        DEBUG_LOG(2, "DEBUG: Send: Failed to read transfer result\n");
        free(meta.name);
        return -1;
    }
    DEBUG_LOG(2, "DEBUG: Send: Transfer result: ok=%d, bytes=%llu\n", 
            result.ok, (unsigned long long)result.received_bytes);
    if (!result.ok) {
        fprintf(stderr, "Transfer failed\n");
        free(meta.name);
        return -1;
    }

    free(meta.name);
    return 0;
}

static int send_directory(Socket* sock, const char* dir_path, OverwriteMode overwrite_mode) {
    FileEntryArray* entries = walk_directory(dir_path);
    if (!entries) {
        fprintf(stderr, "Failed to walk directory: %s\n", dir_path);
        return -1;
    }

    LOG_OUTPUT("Directory contains %zu entries\n", entries->count);

    // Create a single FILE stream for the entire directory transfer
    int fd_dup = dup(sock->fd);
    if (fd_dup < 0) {
        fprintf(stderr, "Failed to duplicate socket descriptor: %s\n", strerror(errno));
        file_entry_array_free(entries);
        return -1;
    }

    FILE* sock_file = fdopen(fd_dup, "w+b");
    if (!sock_file) {
        fprintf(stderr, "Failed to create socket stream: %s\n", strerror(errno));
        close(fd_dup);
        file_entry_array_free(entries);
        return -1;
    }

    // Disable buffering to ensure immediate writes/reads
    if (setvbuf(sock_file, NULL, _IONBF, 0) != 0) {
        fprintf(stderr, "Warning: Failed to disable buffering: %s\n", strerror(errno));
    }
    DEBUG_LOG(2, "DEBUG: Send: Directory socket stream created with unbuffered I/O\n");

    int ret = 0;
    // Process all entries, including the root directory
    // They'll be in the right order (root dir first, then subdirs, then files)
    for (size_t i = 0; i < entries->count; i++) {
        FileEntry* entry = &entries->entries[i];
        
        LOG_OUTPUT("Transferring %s: %s\n", 
            entry->is_dir ? "directory" : "file",
            entry->relative_path);

        // Send each file/directory using the shared stream
        if (send_directory_entry(sock_file, sock, entry, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send '%s'\n", entry->path);
            ret = -1;
            break;
        }
    }

    fclose(sock_file);
    file_entry_array_free(entries);
    return ret;
}

int ncp_execute_send(const char* host, uint16_t port,
                    const char* src_path, uint32_t retries,
                    OverwriteMode overwrite_mode) {
    struct stat st;
    if (stat(src_path, &st) != 0) {
        fprintf(stderr, "Error: Source path '%s' does not exist\n", src_path);
        return 1;
    }

    // Get the base name for the source path
    char* base_name = strdup(src_path);
    if (!base_name) {
        fprintf(stderr, "Failed to allocate memory for basename\n");
        return 1;
    }

    // Create socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Failed to create socket");
        free(base_name);
        return 1;
    }

    // Setup server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address/Address not supported\n");
        close(sock_fd);
        free(base_name);
        return 1;
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
            free(base_name);
            return 1;
        }
        
        fprintf(stderr, "Connection attempt %d failed, retrying...\n", retry_count + 1);
        sleep(1);
        retry_count++;
    }

    Socket* sock = socket_new(sock_fd);
    if (!sock) {
        close(sock_fd);
        free(base_name);
        return 1;
    }

    int result = 0;
    if (S_ISDIR(st.st_mode)) {
        // Send just the directory contents without the root directory
        if (send_directory(sock, src_path, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send: %s\n", src_path);
            result = 1;
        }
    } else {
        // For single files
        if (send_file(sock, src_path, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send: %s\n", src_path);
            result = 1;
        }
    }

    free(base_name);
    socket_free(sock);
    return result;
}

int ncp_execute_send_listen(uint16_t port, const char* src_path,
                          OverwriteMode overwrite_mode) {
    struct stat st;
    if (stat(src_path, &st) != 0) {
        fprintf(stderr, "Error: Source path '%s' does not exist\n", src_path);
        return 1;
    }

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Failed to create socket");
        return 1;
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
        return 1;
    }

    // Bind
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    // Listen
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    LOG_OUTPUT("Listening on port %d...\n", port);

    // Accept connection
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_sock < 0) {
        perror("Accept failed");
        close(server_fd);
        return 1;
    }

    Socket* sock = socket_new(client_sock);
    if (!sock) {
        close(client_sock);
        close(server_fd);
        return 1;
    }

    // Get the base name for the source path
    char* base_name = strdup(src_path);
    if (!base_name) {
        fprintf(stderr, "Failed to allocate memory for basename\n");
        socket_free(sock);
        close(server_fd);
        return 1;
    }

    int result = 0;
    if (S_ISDIR(st.st_mode)) {
        LOG_OUTPUT("Sending directory: %s\n", src_path);
        // Send just the directory contents without the root directory
        if (send_directory(sock, src_path, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send: %s\n", src_path);
            result = 1;
        }
    } else {
        LOG_OUTPUT("Sending file: %s\n", src_path);
        if (send_file(sock, src_path, overwrite_mode) < 0) {
            fprintf(stderr, "Failed to send: %s\n", src_path);
            result = 1;
        }
    }

    free(base_name);
    socket_free(sock);
    close(server_fd);
    return result;
}
