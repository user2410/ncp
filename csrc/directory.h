#ifndef NCP_DIRECTORY_H
#define NCP_DIRECTORY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Represents a file or directory entry with its metadata
 */
typedef struct {
    char* path;              // Full path to the file/directory
    char* relative_path;     // Path relative to root directory
    int is_dir;             // 1 if directory, 0 if file
    uint64_t size;          // File size in bytes (0 for directories)
} FileEntry;

/**
 * Dynamic array of FileEntry structures
 */
typedef struct {
    FileEntry* entries;      // Array of entries
    size_t count;           // Number of entries in use
    size_t capacity;        // Total allocated capacity
} FileEntryArray;

/**
 * Initialize a new FileEntryArray
 * @return Pointer to new FileEntryArray or NULL on error
 */
FileEntryArray* file_entry_array_new(void);

/**
 * Free a FileEntryArray and all its entries
 * @param array Pointer to the array to free
 */
void file_entry_array_free(FileEntryArray* array);

/**
 * Recursively walk directory and collect all files and directories
 * @param root Path to the root directory to walk
 * @return Array of file entries or NULL on error
 */
FileEntryArray* walk_directory(const char* root);

/**
 * Calculate total size of all files in entries
 * @param entries Array of file entries
 * @return Total size in bytes
 */
uint64_t calculate_total_size(const FileEntryArray* entries);

#ifdef __cplusplus
}
#endif

#endif /* NCP_DIRECTORY_H */
