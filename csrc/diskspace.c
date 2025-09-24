#include "diskspace.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifdef _WIN32
#define PATH_MAX MAX_PATH
#elif defined(__APPLE__)
// PATH_MAX is defined in <limits.h> on macOS
#else
#include <linux/limits.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/statvfs.h>
#include <libgen.h>
#include <unistd.h>
#endif

uint64_t get_available_space(const char* path) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    // Make a copy of path since we might need to modify it
    char path_copy[PATH_MAX];
    strncpy(path_copy, path, PATH_MAX - 1);
    path_copy[PATH_MAX - 1] = '\0';

    // If path doesn't exist, check its parent directory
    if (access(path_copy, F_OK) != 0) {
        char* parent = dirname(path_copy);
        if (!parent || strlen(parent) == 0) {
            strcpy(path_copy, ".");
        } else {
            strncpy(path_copy, parent, PATH_MAX - 1);
            path_copy[PATH_MAX - 1] = '\0';
        }
    }

#ifdef _WIN32
    ULARGE_INTEGER free_bytes_available;
    ULARGE_INTEGER total_number_of_bytes;
    ULARGE_INTEGER total_number_of_free_bytes;

    if (!GetDiskFreeSpaceExA(
        path_copy,
        &free_bytes_available,
        &total_number_of_bytes,
        &total_number_of_free_bytes)) {
        return -1;
    }

    return free_bytes_available.QuadPart;
#else
    struct statvfs stat;
    if (statvfs(path_copy, &stat) != 0) {
        return -1;
    }

    // Available space = available blocks * block size
    return (uint64_t)stat.f_bavail * (uint64_t)stat.f_frsize;
#endif
}

int check_disk_space(const char* path, uint64_t required_bytes) {
    uint64_t available = get_available_space(path);
    if (available == (uint64_t)-1) {
        return 0;  // Error case
    }

    // Add 10% buffer for safety
    uint64_t buffer = required_bytes / 10;
    uint64_t required_with_buffer = required_bytes + buffer;

    // Check for overflow
    if (required_with_buffer < required_bytes) {
        required_with_buffer = UINT64_MAX;
    }

    return available >= required_with_buffer;
}

int format_bytes(uint64_t bytes, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }

    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    const uint64_t threshold = 1024;
    
    if (bytes < threshold) {
        int ret = snprintf(buffer, buffer_size, "%lu B", (unsigned long)bytes);
        return (ret < 0 || (size_t)ret >= buffer_size) ? -1 : 0;
    }

    double size = (double)bytes;
    int unit_index = 0;

    while (size >= threshold && unit_index < 4) {
        size /= threshold;
        unit_index++;
    }

    int ret = snprintf(buffer, buffer_size, "%.1f %s", size, units[unit_index]);
    return (ret < 0 || (size_t)ret >= buffer_size) ? -1 : 0;
}
