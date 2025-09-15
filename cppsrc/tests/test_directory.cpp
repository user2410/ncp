#include "directory.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cassert>

namespace fs = std::filesystem;

// Simple test framework
#define TEST(name) \
    void test_##name(); \
    struct test_##name##_runner { \
        test_##name##_runner() { \
            std::cout << "Running " #name "... "; \
            try { \
                test_##name(); \
                std::cout << "PASS\n"; \
            } catch (const std::exception& e) { \
                std::cout << "FAIL: " << e.what() << "\n"; \
                exit(1); \
            } \
        } \
    } test_##name##_instance; \
    void test_##name()

// Test helper to create test directory structure
void setup_test_dir() {
    fs::remove_all("test_dir");
    fs::create_directories("test_dir/subdir");
    
    std::ofstream("test_dir/file1.txt") << "content1";
    std::ofstream("test_dir/file2.txt") << "content2";
    std::ofstream("test_dir/subdir/file3.txt") << "content3";
}

void cleanup_test_dir() {
    fs::remove_all("test_dir");
}

TEST(walk_directory_basic) {
    setup_test_dir();
    
    auto entries = ncp::walk_directory("test_dir");
    
    // Should have: test_dir, subdir, file1.txt, file2.txt, file3.txt
    assert(entries.size() == 5);
    
    // Check directories come first
    assert(entries[0].relative_path == ".");
    assert(entries[0].is_dir == true);
    assert(entries[1].relative_path == "subdir");
    assert(entries[1].is_dir == true);
    
    // Check files
    bool found_file1 = false, found_file2 = false, found_file3 = false;
    for (const auto& entry : entries) {
        if (entry.relative_path == "file1.txt") {
            assert(!entry.is_dir);
            assert(entry.size == 8); // "content1"
            found_file1 = true;
        } else if (entry.relative_path == "file2.txt") {
            assert(!entry.is_dir);
            assert(entry.size == 8); // "content2"
            found_file2 = true;
        } else if (entry.relative_path == "subdir/file3.txt") {
            assert(!entry.is_dir);
            assert(entry.size == 8); // "content3"
            found_file3 = true;
        }
    }
    assert(found_file1 && found_file2 && found_file3);
    
    cleanup_test_dir();
}

TEST(calculate_total_size) {
    setup_test_dir();
    
    auto entries = ncp::walk_directory("test_dir");
    auto total_size = ncp::calculate_total_size(entries);
    
    // 3 files * 8 bytes each = 24 bytes
    assert(total_size == 24);
    
    cleanup_test_dir();
}

TEST(empty_directory) {
    fs::remove_all("empty_dir");
    fs::create_directory("empty_dir");
    
    auto entries = ncp::walk_directory("empty_dir");
    
    // Should only have the root directory
    assert(entries.size() == 1);
    assert(entries[0].relative_path == ".");
    assert(entries[0].is_dir == true);
    
    auto total_size = ncp::calculate_total_size(entries);
    assert(total_size == 0);
    
    fs::remove_all("empty_dir");
}

int main() {
    std::cout << "Running directory module tests...\n";
    // Tests run automatically via static constructors
    std::cout << "All tests passed!\n";
    return 0;
}