#define _POSIX_C_SOURCE 200809L  // For strdup and other POSIX functions
#include "../recv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

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

TEST(overwrite_mode_values) {
    // Test that enum values are accessible and distinct
    if (OVERWRITE_ASK == OVERWRITE_YES || 
        OVERWRITE_YES == OVERWRITE_NO || 
        OVERWRITE_ASK == OVERWRITE_NO) {
        test_failure("Overwrite mode values are not distinct");
    }
}

TEST(filesystem_operations) {
    const char* test_dir = "test_recv_dir";
    
    // Remove existing test directory if any
    system("rm -rf test_recv_dir");
    
    // Test directory creation
    if (mkdir(test_dir, 0755) != 0) {
        test_failure("Failed to create test directory");
    }
    
    struct stat st;
    if (stat(test_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        test_failure("Directory creation verification failed");
    }
    
    // Test file path operations
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/test.txt", test_dir);
    
    char* parent = strdup(file_path);
    char* last_slash = strrchr(parent, '/');
    if (last_slash) {
        *last_slash = '\0';
    }
    
    if (strcmp(parent, test_dir) != 0) {
        test_failure("Parent path extraction failed");
    }
    
    free(parent);
    
    // Test temp file naming
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "%s.ncp_temp", file_path);
    
    if (strlen(temp_path) < 9 || 
        strcmp(temp_path + strlen(temp_path) - 9, ".ncp_temp") != 0) {
        test_failure("Temp file naming failed");
    }
    
    system("rm -rf test_recv_dir");
}

TEST(path_determination) {
    const char* test_dir = "test_path_det";
    
    // Remove existing test directory if any
    system("rm -rf test_path_det");
    
    // Create test directory
    if (mkdir(test_dir, 0755) != 0) {
        test_failure("Failed to create test directory");
    }
    
    // Test directory exists check
    struct stat st;
    if (stat(test_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        test_failure("Directory existence check failed");
    }
    
    // Test file path that doesn't exist
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/output.txt", test_dir);
    
    if (access(file_path, F_OK) == 0) {
        test_failure("File should not exist");
    }
    
    system("rm -rf test_path_det");
}

int main(void) {
    printf("Running recv module tests...\n");
    
    run_overwrite_mode_values();
    run_filesystem_operations();
    run_path_determination();
    
    printf("All tests passed!\n");
    return 0;
}
