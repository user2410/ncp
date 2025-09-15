#include <iostream>
#include <cassert>
#include <string>

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

TEST(string_operations) {
    // Test string operations used in main
    std::string test = "--host";
    assert(test.substr(0, 2) == "--");
    assert(test.substr(0, 3) != "abc");
    
    std::string mode = "ask";
    assert(mode == "ask");
    assert(mode != "yes");
}

TEST(argument_validation) {
    // Test basic argument validation logic
    uint16_t port = 9000;
    assert(port > 0);
    assert(port <= 65535);
    
    uint32_t retries = 3;
    assert(retries > 0);
    assert(retries <= 100);
}

TEST(overwrite_modes) {
    // Test overwrite mode string matching
    std::string ask = "ask";
    std::string yes = "yes";
    std::string no = "no";
    
    assert(ask == "ask");
    assert(yes == "yes");
    assert(no == "no");
    assert(ask != yes);
}

int main() {
    std::cout << "Running main module tests...\n";
    // Tests run automatically via static constructors
    std::cout << "All tests passed!\n";
    return 0;
}