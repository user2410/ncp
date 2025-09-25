#define _POSIX_C_SOURCE 200809L  // For strdup and other POSIX functions
#include "directory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define INITIAL_CAPACITY 16

// Helper function to duplicate a string
static char* strdup_safe(const char* str) {
    if (!str) return NULL;
    char* dup = strdup(str);
    if (!dup) {
        errno = ENOMEM;
        return NULL;
    }
    return dup;
}

// Helper function to create a new file entry
static FileEntry create_file_entry(const char* full_path, const char* rel_path, int is_dir, uint64_t size) {
    FileEntry entry = {0};
    entry.path = strdup_safe(full_path);
    entry.relative_path = strdup_safe(rel_path);
    entry.is_dir = is_dir;
    entry.size = size;
    return entry;
}

// Free resources used by a file entry
static void file_entry_free(FileEntry* entry) {
    if (!entry) return;
    free(entry->path);
    free(entry->relative_path);
    entry->path = NULL;
    entry->relative_path = NULL;
}

FileEntryArray* file_entry_array_new(void) {
    FileEntryArray* array = malloc(sizeof(FileEntryArray));
    if (!array) return NULL;
    
    array->entries = malloc(sizeof(FileEntry) * INITIAL_CAPACITY);
    if (!array->entries) {
        free(array);
        return NULL;
    }
    
    array->count = 0;
    array->capacity = INITIAL_CAPACITY;
    return array;
}

void file_entry_array_free(FileEntryArray* array) {
    if (!array) return;
    
    for (size_t i = 0; i < array->count; i++) {
        file_entry_free(&array->entries[i]);
    }
    
    free(array->entries);
    free(array);
}

// Add an entry to the array, resizing if necessary
static int file_entry_array_add(FileEntryArray* array, FileEntry entry) {
    if (array->count >= array->capacity) {
        size_t new_capacity = array->capacity * 2;
        FileEntry* new_entries = realloc(array->entries, sizeof(FileEntry) * new_capacity);
        if (!new_entries) return -1;
        
        array->entries = new_entries;
        array->capacity = new_capacity;
    }
    
    array->entries[array->count++] = entry;
    return 0;
}

// Compare function for sorting entries
static int compare_entries(const void* a, const void* b) {
    const FileEntry* entry_a = (const FileEntry*)a;
    const FileEntry* entry_b = (const FileEntry*)b;
    
    // Directories before files
    if (entry_a->is_dir != entry_b->is_dir) {
        return entry_b->is_dir - entry_a->is_dir;  // Directories first (like C++ implementation)
    }
    
    // Sort by relative path
    return strcmp(entry_a->relative_path, entry_b->relative_path);
}

// Helper function to get relative path
static char* get_relative_path(const char* root, const char* full_path) {
    size_t root_len = strlen(root);
    const char* rel = full_path + root_len;
    
    // Skip leading slash
    while (*rel == '/') rel++;
    
    // Handle empty relative path (root directory)
    if (*rel == '\0') return strdup_safe(".");
    
    return strdup_safe(rel);
}

// Recursive function to walk directory
static int walk_recursive(const char* root, const char* current_path, FileEntryArray* array) {
    DIR* dir = opendir(current_path);
    if (!dir) return -1;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Construct full path
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", current_path, entry->d_name);
        
        // Get file status
        struct stat st;
        if (lstat(full_path, &st) == -1) {
            continue;  // Skip on error
        }
        
        // Get relative path
        char* rel_path = get_relative_path(root, full_path);
        if (!rel_path) {
            closedir(dir);
            return -1;
        }
        
        // Create and add entry (file or directory)
        FileEntry new_entry = create_file_entry(
            full_path,
            rel_path,
            S_ISDIR(st.st_mode),
            S_ISREG(st.st_mode) ? st.st_size : 0
        );
        
        if (!new_entry.path || !new_entry.relative_path ||
            file_entry_array_add(array, new_entry) != 0) {
            file_entry_free(&new_entry);
            free(rel_path);
            closedir(dir);
            return -1;
        }
        
        // If it's a directory, process its contents after adding it
        if (S_ISDIR(st.st_mode)) {
            if (walk_recursive(root, full_path, array) != 0) {
                free(rel_path);
                closedir(dir);
                return -1;
            }
        }
        
        free(rel_path);  // Already duplicated in create_file_entry
    }
    
    closedir(dir);
    return 0;
}

FileEntryArray* walk_directory(const char* root) {
    if (!root) {
        errno = EINVAL;
        return NULL;
    }
    
    // Create result array
    FileEntryArray* array = file_entry_array_new();
    if (!array) return NULL;

    // Add root directory entry first
    FileEntry root_entry = create_file_entry(root, ".", 1, 0);
    if (!root_entry.path || !root_entry.relative_path ||
        file_entry_array_add(array, root_entry) != 0) {
        file_entry_free(&root_entry);
        file_entry_array_free(array);
        return NULL;
    }
    
    // Walk the directory tree
    if (walk_recursive(root, root, array) != 0) {
        file_entry_array_free(array);
        return NULL;
    }
    
    // Sort entries to ensure proper order (directories first, then files alphabetically)
    qsort(array->entries, array->count, sizeof(FileEntry), compare_entries);
    
    return array;
}

uint64_t calculate_total_size(const FileEntryArray* entries) {
    if (!entries) return 0;
    
    uint64_t total = 0;
    for (size_t i = 0; i < entries->count; i++) {
        if (!entries->entries[i].is_dir) {
            total += entries->entries[i].size;
        }
    }
    
    return total;
}
