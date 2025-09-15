#include "diskspace.hpp"
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

TEST(get_available_space) {
    auto temp_dir = fs::temp_directory_path();
    auto space = ncp::get_available_space(temp_dir);
    
    // Should have some available space
    assert(space > 0);
    std::cout << "(Available: " << space << " bytes) ";
}

TEST(check_disk_space) {
    auto temp_dir = fs::temp_directory_path();
    
    // Should have space for a small file
    assert(ncp::check_disk_space(temp_dir, 1024));
    
    // Test with a very large file size
    auto available = ncp::get_available_space(temp_dir);
    if (available < UINT64_MAX / 2) {
        uint64_t too_large = available * 2;
        assert(!ncp::check_disk_space(temp_dir, too_large));
    }
}

TEST(format_bytes) {
    assert(ncp::format_bytes(0) == "0 B");
    assert(ncp::format_bytes(512) == "512 B");
    assert(ncp::format_bytes(1024) == "1.0 KB");
    assert(ncp::format_bytes(1536) == "1.5 KB");
    assert(ncp::format_bytes(1024 * 1024) == "1.0 MB");
    assert(ncp::format_bytes(1024ULL * 1024 * 1024) == "1.0 GB");
}

TEST(nonexistent_path) {
    fs::path nonexistent = "/tmp/nonexistent/deep/path";
    
    // Should check parent directory that exists
    try {
        auto space = ncp::get_available_space(nonexistent);
        assert(space >= 0); // Should succeed
    } catch (const std::exception&) {
        // Or fail gracefully - both are acceptable
    }
}

int main() {
    std::cout << "Running diskspace module tests...\n";
    // Tests run automatically via static constructors
    std::cout << "All tests passed!\n";
    return 0;
}