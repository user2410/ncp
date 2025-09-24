#ifndef NCP_RECV_H
#define NCP_RECV_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Overwrite mode enumeration
typedef enum {
    OVERWRITE_ASK,  // Ask before overwriting
    OVERWRITE_YES,  // Always overwrite
    OVERWRITE_NO    // Never overwrite
} OverwriteMode;

// Socket structure
typedef struct Socket Socket;

// Create a new socket structure
Socket* socket_new(int fd);

// Free socket resources
void socket_free(Socket* sock);

// Write data to socket
int socket_write(Socket* sock, const void* data, size_t size);

// Read data from socket
int socket_read(Socket* sock, void* data, size_t size);

// Execute receiver in listen mode
int recv_execute(const char* host, uint16_t port, 
                const char* dst_path, OverwriteMode overwrite_mode);

// Execute receiver in connect mode
int recv_execute_connect(const char* host, uint16_t port,
                        const char* dst_path, OverwriteMode overwrite_mode);

// Get string representation of last error
const char* recv_get_error(void);

#ifdef __cplusplus
}
#endif

#endif /* NCP_RECV_H */
