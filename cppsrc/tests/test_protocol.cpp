#include "protocol.hpp"
#include <iostream>
#include <sstream>
#include <cassert>

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

TEST(meta_roundtrip) {
    ncp::FileMeta original{"test.txt", 1024, false};
    
    std::stringstream stream;
    ncp::write_meta(stream, original);
    
    // Read back message type and length
    stream.seekg(0);
    auto msg_type = ncp::read_message_type(stream);
    auto msg_len = ncp::read_message_length(stream);
    
    assert(msg_type == ncp::MSG_META);
    assert(msg_len == 8 + 1 + 4 + 8); // size + is_dir + name_len + name
    
    auto result = ncp::read_meta(stream);
    assert(result.name == original.name);
    assert(result.size == original.size);
    assert(result.is_dir == original.is_dir);
}

TEST(preflight_ok_roundtrip) {
    ncp::PreflightOk original{123456789};
    
    std::stringstream stream;
    ncp::write_preflight_ok(stream, original);
    
    stream.seekg(0);
    auto msg_type = ncp::read_message_type(stream);
    auto msg_len = ncp::read_message_length(stream);
    
    assert(msg_type == ncp::MSG_PREFLIGHT_OK);
    assert(msg_len == 8);
    
    auto result = ncp::read_preflight_ok(stream);
    assert(result.available_space == original.available_space);
}

TEST(preflight_fail_roundtrip) {
    ncp::PreflightFail original{"Not enough space"};
    
    std::stringstream stream;
    ncp::write_preflight_fail(stream, original);
    
    stream.seekg(0);
    auto msg_type = ncp::read_message_type(stream);
    auto msg_len = ncp::read_message_length(stream);
    
    assert(msg_type == ncp::MSG_PREFLIGHT_FAIL);
    assert(msg_len == 4 + original.reason.size());
    
    auto result = ncp::read_preflight_fail(stream);
    assert(result.reason == original.reason);
}

TEST(transfer_start_roundtrip) {
    ncp::TransferStart original{987654321};
    
    std::stringstream stream;
    ncp::write_transfer_start(stream, original);
    
    stream.seekg(0);
    auto msg_type = ncp::read_message_type(stream);
    auto msg_len = ncp::read_message_length(stream);
    
    assert(msg_type == ncp::MSG_TRANSFER_START);
    assert(msg_len == 8);
    
    auto result = ncp::read_transfer_start(stream);
    assert(result.file_size == original.file_size);
}

TEST(transfer_result_roundtrip) {
    ncp::TransferResult original{true, 555666777};
    
    std::stringstream stream;
    ncp::write_transfer_result(stream, original);
    
    stream.seekg(0);
    auto msg_type = ncp::read_message_type(stream);
    auto msg_len = ncp::read_message_length(stream);
    
    assert(msg_type == ncp::MSG_TRANSFER_RESULT);
    assert(msg_len == 9); // 1 byte bool + 8 bytes uint64
    
    auto result = ncp::read_transfer_result(stream);
    assert(result.ok == original.ok);
    assert(result.received_bytes == original.received_bytes);
}

TEST(directory_meta) {
    ncp::FileMeta dir_meta{"my_folder", 0, true};
    
    std::stringstream stream;
    ncp::write_meta(stream, dir_meta);
    
    stream.seekg(0);
    ncp::read_message_type(stream);
    ncp::read_message_length(stream);
    
    auto result = ncp::read_meta(stream);
    assert(result.name == "my_folder");
    assert(result.size == 0);
    assert(result.is_dir == true);
}

int main() {
    std::cout << "Running protocol module tests...\n";
    // Tests run automatically via static constructors
    std::cout << "All tests passed!\n";
    return 0;
}