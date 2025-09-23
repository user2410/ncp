#ifndef NCP_DISKSPACE_H
#define NCP_DISKSPACE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get available disk space for a given path
 * @param path The filesystem path to check
 * @return Available space in bytes, or -1 on error
 */
uint64_t get_available_space(const char* path);

/**
 * Check if there's enough disk space for a file transfer
 * @param path The filesystem path to check
 * @param required_bytes The number of bytes needed
 * @return 1 if enough space is available, 0 if not enough space or error
 */
int check_disk_space(const char* path, uint64_t required_bytes);

/**
 * Format bytes in human-readable format
 * @param bytes The number of bytes to format
 * @param buffer The output buffer to store the formatted string
 * @param buffer_size The size of the output buffer
 * @return 0 on success, -1 on error
 */
int format_bytes(uint64_t bytes, char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* NCP_DISKSPACE_H */
