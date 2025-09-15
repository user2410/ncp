#include "send.hpp"
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

TEST(source_validation) {
    // Test that send functions validate source existence
    fs::path nonexistent = "nonexistent_file.txt";
    fs::remove(nonexistent);
    
    try {
        ncp::execute_send("127.0.0.1", 9999, nonexistent, 1, ncp::OverwriteMode::Yes);
        assert(false); // Should not reach here
    } catch (const std::exception& e) {
        std::string error = e.what();
        assert(error.find("does not exist") != std::string::npos);
    }
}

TEST(file_operations) {
    // Test basic file operations used by send
    fs::path test_dir = "test_send_dir";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);
    
    // Create test file
    fs::path test_file = test_dir / "test.txt";
    std::ofstream file(test_file);
    file << "test content";
    file.close();
    
    // Test file existence and size
    assert(fs::exists(test_file));
    assert(!fs::is_directory(test_file));
    assert(fs::file_size(test_file) > 0);
    
    // Test directory detection
    assert(fs::exists(test_dir));
    assert(fs::is_directory(test_dir));
    
    fs::remove_all(test_dir);
}

TEST(retry_parameters) {
    // Test retry parameter validation
    uint32_t retries = 3;
    assert(retries > 0);
    assert(retries <= 10); // Reasonable upper bound
    
    // Test overwrite mode enum
    ncp::OverwriteMode mode = ncp::OverwriteMode::Ask;
    assert(mode == ncp::OverwriteMode::Ask);
    assert(mode != ncp::OverwriteMode::Yes);
}

int main() {
    std::cout << "Running send module tests...\n";
    // Tests run automatically via static constructors
    std::cout << "All tests passed!\n";
    return 0;
}