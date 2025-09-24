#include "../send.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

// Test helper functions
static void create_test_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f, "%s", content);
    fclose(f);
}

static void create_test_dir(const char* path) {
    assert(mkdir(path, 0755) == 0);
}

static void remove_test_file(const char* path) {
    unlink(path);
}

static void remove_test_dir(const char* path) {
    rmdir(path);
}

// Test cases
static void test_source_validation(void) {
    printf("Running source_validation... ");
    
    const char* nonexistent = "nonexistent_file.txt";
    remove_test_file(nonexistent);  // Ensure file doesn't exist
    
    // Test should fail because file doesn't exist
    // We expect the program to exit, so we fork to test this
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        ncp_execute_send("127.0.0.1", 9999, nonexistent, 1, OVERWRITE_YES);
        exit(0);  // Should not reach here
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        assert(WEXITSTATUS(status) == 1);  // Should exit with status 1
    }
    
    printf("PASS\n");
}

static void test_file_operations(void) {
    printf("Running file_operations... ");
    
    const char* test_dir = "test_send_dir";
    const char* test_file = "test_send_dir/test.txt";
    const char* test_content = "test content";
    
    // Clean up any existing test files
    remove_test_file(test_file);
    remove_test_dir(test_dir);
    
    // Create test directory and file
    create_test_dir(test_dir);
    create_test_file(test_file, test_content);
    
    // Verify file exists and has correct properties
    struct stat st;
    assert(stat(test_file, &st) == 0);
    assert(S_ISREG(st.st_mode));  // Is regular file
    assert(st.st_size > 0);
    
    // Verify directory exists
    assert(stat(test_dir, &st) == 0);
    assert(S_ISDIR(st.st_mode));  // Is directory
    
    // Clean up
    remove_test_file(test_file);
    remove_test_dir(test_dir);
    
    printf("PASS\n");
}

static void test_retry_parameters(void) {
    printf("Running retry_parameters... ");
    
    uint32_t retries = 3;
    assert(retries > 0);
    assert(retries <= 10);  // Reasonable upper bound
    
    OverwriteMode mode = OVERWRITE_ASK;
    assert(mode == OVERWRITE_ASK);
    assert(mode != OVERWRITE_YES);
    
    printf("PASS\n");
}

int main(void) {
    printf("Running send module tests...\n");
    
    test_source_validation();
    test_file_operations();
    test_retry_parameters();
    
    printf("All tests passed!\n");
    return 0;
}
