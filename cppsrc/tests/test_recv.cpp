#include "recv.hpp"
#include <iostream>
#include <filesystem>
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

TEST(overwrite_mode_enum) {
    // Test that enum values are accessible
    ncp::OverwriteMode ask = ncp::OverwriteMode::Ask;
    ncp::OverwriteMode yes = ncp::OverwriteMode::Yes;
    ncp::OverwriteMode no = ncp::OverwriteMode::No;
    
    // Basic enum comparison
    assert(ask != yes);
    assert(yes != no);
    assert(ask != no);
}

TEST(filesystem_operations) {
    // Test basic filesystem operations used by recv
    fs::path test_dir = "test_recv_dir";
    fs::remove_all(test_dir);
    
    // Test directory creation
    fs::create_directories(test_dir);
    assert(fs::exists(test_dir));
    assert(fs::is_directory(test_dir));
    
    // Test file path operations
    fs::path file_path = test_dir / "test.txt";
    assert(file_path.has_parent_path());
    assert(file_path.parent_path() == test_dir);
    
    // Test temp file naming
    fs::path temp_path = file_path;
    temp_path += ".ncp_temp";
    std::string temp_str = temp_path.string();
    assert(temp_str.substr(temp_str.length() - 9) == ".ncp_temp");
    
    fs::remove_all(test_dir);
}

TEST(path_determination) {
    fs::path test_dir = "test_path_det";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);
    
    // Test directory destination
    fs::path dir_dst = test_dir;
    assert(fs::is_directory(dir_dst));
    
    // Test file destination
    fs::path file_dst = test_dir / "output.txt";
    assert(!fs::exists(file_dst));
    
    fs::remove_all(test_dir);
}

int main() {
    std::cout << "Running recv module tests...\n";
    // Tests run automatically via static constructors
    std::cout << "All tests passed!\n";
    return 0;
}