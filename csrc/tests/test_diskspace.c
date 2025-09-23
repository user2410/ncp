#include "../diskspace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

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

TEST(get_available_space) {
    char temp_dir[] = "/tmp";  // Use system temp directory
    uint64_t space = get_available_space(temp_dir);
    
    // Should have some available space
    if (space == (uint64_t)-1) {
        test_failure("Failed to get available space");
    }
    if (space == 0) {
        test_failure("Available space is 0");
    }
    printf("(Available: %lu bytes) ", (unsigned long)space);
}

TEST(check_disk_space) {
    char temp_dir[] = "/tmp";  // Use system temp directory
    
    // Should have space for a small file
    if (!check_disk_space(temp_dir, 1024)) {
        test_failure("Failed disk space check for small file");
    }
    
    // Test with a very large file size
    uint64_t available = get_available_space(temp_dir);
    if (available != (uint64_t)-1 && available < UINT64_MAX / 2) {
        uint64_t too_large = available * 2;
        if (check_disk_space(temp_dir, too_large)) {
            test_failure("Disk space check should fail for too large file");
        }
    }
}

TEST(format_bytes) {
    char buffer[32];
    
    if (format_bytes(0, buffer, sizeof(buffer)) != 0 ||
        strcmp(buffer, "0 B") != 0) {
        test_failure("format_bytes failed for 0");
    }
    
    if (format_bytes(512, buffer, sizeof(buffer)) != 0 ||
        strcmp(buffer, "512 B") != 0) {
        test_failure("format_bytes failed for 512");
    }
    
    if (format_bytes(1024, buffer, sizeof(buffer)) != 0 ||
        strcmp(buffer, "1.0 KB") != 0) {
        test_failure("format_bytes failed for 1024");
    }
    
    if (format_bytes(1536, buffer, sizeof(buffer)) != 0 ||
        strcmp(buffer, "1.5 KB") != 0) {
        test_failure("format_bytes failed for 1536");
    }
    
    if (format_bytes(1024ULL * 1024, buffer, sizeof(buffer)) != 0 ||
        strcmp(buffer, "1.0 MB") != 0) {
        test_failure("format_bytes failed for 1MB");
    }
    
    if (format_bytes(1024ULL * 1024 * 1024, buffer, sizeof(buffer)) != 0 ||
        strcmp(buffer, "1.0 GB") != 0) {
        test_failure("format_bytes failed for 1GB");
    }
}

TEST(nonexistent_path) {
    const char* nonexistent = "/tmp/nonexistent/deep/path";
    
    // Should check parent directory that exists
    uint64_t space = get_available_space(nonexistent);
    if (space == (uint64_t)-1) {
        printf("(Non-existent path returns error - acceptable) ");
    } else {
        printf("(Non-existent path returns available space - acceptable) ");
    }
}

int main(void) {
    printf("Running diskspace module tests...\n");
    
    run_get_available_space();
    run_check_disk_space();
    run_format_bytes();
    run_nonexistent_path();
    
    printf("All tests passed!\n");
    return 0;
}
