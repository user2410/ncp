#include "../directory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

// Simple test framework
#define TEST(name) \
    static void test_##name(void); \
    static void run_##name(void) { \
        printf("Running " #name "... "); \
        fflush(stdout); \
        test_##name(); \
        printf("PASS\n"); \
    } \
    static void test_##name(void)

static void test_failure(const char* message) {
    printf("FAIL: %s\n", message);
    exit(1);
}

// Helper to create a file with content
static void create_file_with_content(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) test_failure("Failed to create test file");
    if (fputs(content, f) == EOF) test_failure("Failed to write test content");
    fclose(f);
}

// Test helper to create test directory structure
static void setup_test_dir(void) {
    // Remove existing test directory if any
    (void) system("rm -rf test_dir");
    
    // Create directory structure
    if (mkdir("test_dir", 0755) != 0 ||
        mkdir("test_dir/subdir", 0755) != 0) {
        test_failure("Failed to create test directories");
    }
    
    // Create test files with content
    create_file_with_content("test_dir/file1.txt", "content1");
    create_file_with_content("test_dir/file2.txt", "content2");
    create_file_with_content("test_dir/subdir/file3.txt", "content3");
}

static void cleanup_test_dir(void) {
    (void) system("rm -rf test_dir");
}

TEST(walk_directory_basic) {
    setup_test_dir();
    
    FileEntryArray* entries = walk_directory("test_dir");
    if (!entries) test_failure("walk_directory returned NULL");
    
    // Should have: test_dir, subdir, file1.txt, file2.txt, file3.txt
    if (entries->count != 5) {
        char msg[100];
        snprintf(msg, sizeof(msg), "Expected 5 entries, got %zu", entries->count);
        test_failure(msg);
    }
    
    // Check directories come first
    if (strcmp(entries->entries[0].relative_path, ".") != 0 ||
        !entries->entries[0].is_dir) {
        test_failure("First entry should be root directory");
    }
    
    if (strcmp(entries->entries[1].relative_path, "subdir") != 0 ||
        !entries->entries[1].is_dir) {
        test_failure("Second entry should be subdir");
    }
    
    // Check files
    int found_file1 = 0, found_file2 = 0, found_file3 = 0;
    for (size_t i = 0; i < entries->count; i++) {
        FileEntry* entry = &entries->entries[i];
        
        if (strcmp(entry->relative_path, "file1.txt") == 0) {
            if (entry->is_dir) test_failure("file1.txt should not be a directory");
            if (entry->size != 8) test_failure("file1.txt wrong size");
            found_file1 = 1;
        }
        else if (strcmp(entry->relative_path, "file2.txt") == 0) {
            if (entry->is_dir) test_failure("file2.txt should not be a directory");
            if (entry->size != 8) test_failure("file2.txt wrong size");
            found_file2 = 1;
        }
        else if (strcmp(entry->relative_path, "subdir/file3.txt") == 0) {
            if (entry->is_dir) test_failure("file3.txt should not be a directory");
            if (entry->size != 8) test_failure("file3.txt wrong size");
            found_file3 = 1;
        }
    }
    
    if (!found_file1 || !found_file2 || !found_file3) {
        test_failure("Not all expected files were found");
    }
    
    file_entry_array_free(entries);
    cleanup_test_dir();
}

TEST(calculate_total_size) {
    setup_test_dir();
    
    FileEntryArray* entries = walk_directory("test_dir");
    if (!entries) test_failure("walk_directory returned NULL");
    
    uint64_t total_size = calculate_total_size(entries);
    
    // 3 files * 8 bytes each = 24 bytes
    if (total_size != 24) {
        char msg[100];
        snprintf(msg, sizeof(msg), "Expected total size 24, got %lu", (unsigned long)total_size);
        test_failure(msg);
    }
    
    file_entry_array_free(entries);
    cleanup_test_dir();
}

TEST(empty_directory) {
    (void) system("rm -rf empty_dir");
    if (mkdir("empty_dir", 0755) != 0) {
        test_failure("Failed to create empty directory");
    }
    
    FileEntryArray* entries = walk_directory("empty_dir");
    if (!entries) test_failure("walk_directory returned NULL");
    
    // Should only have the root directory
    if (entries->count != 1) {
        char msg[100];
        snprintf(msg, sizeof(msg), "Expected 1 entry, got %zu", entries->count);
        test_failure(msg);
    }
    
    if (strcmp(entries->entries[0].relative_path, ".") != 0 ||
        !entries->entries[0].is_dir) {
        test_failure("First entry should be root directory");
    }
    
    uint64_t total_size = calculate_total_size(entries);
    if (total_size != 0) {
        test_failure("Empty directory should have size 0");
    }
    
    file_entry_array_free(entries);
    (void) system("rm -rf empty_dir");
}

int main(void) {
    printf("Running directory module tests...\n");
    
    run_walk_directory_basic();
    run_calculate_total_size();
    run_empty_directory();
    
    printf("All tests passed!\n");
    return 0;
}
