#define _POSIX_C_SOURCE 200809L  // For strdup
#include "../protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

// Helper function to create a temporary file stream
static FILE* create_memstream(void) {
    FILE* tmp = tmpfile();
    if (!tmp) {
        perror("tmpfile");
        return NULL;
    }
    return tmp;
}

// static void verify_stream(FILE* stream, const char* context) {
//     if (ferror(stream)) {
//         char buf[256];
//         snprintf(buf, sizeof(buf), "%s: Stream error occurred", context);
//         test_failure(buf);
//     }
// }

TEST(meta_roundtrip) {
    FileMeta original = {0};  // Initialize to zero first
    original.name = strdup("test.txt");
    if (!original.name) test_failure("Failed to allocate name");
    original.size = 1024;
    original.is_dir = 0;
    
    FILE* stream = create_memstream();
    if (!stream) test_failure("Failed to create memory stream");
    
    if (write_meta(stream, &original) != 0) {
        test_failure("Failed to write meta");
    }
    
    // Read back message type and length
    rewind(stream);
    uint8_t msg_type;
    uint32_t msg_len;
    
    if (read_message_type(stream, &msg_type) != 0 ||
        read_message_length(stream, &msg_len) != 0) {
        test_failure("Failed to read message header");
    }
    
    if (msg_type != MSG_META) {
        test_failure("Wrong message type");
    }
    
    uint32_t expected_len = 8 + 1 + 1 + 4 + strlen(original.name); // size + is_dir + overwrite_mode + name_len + name
    if (msg_len != expected_len) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Wrong message length: got %u, expected %u", msg_len, expected_len);
        test_failure(buf);
    }
    
    FileMeta result = {0};
    if (read_meta(stream, &result) != 0) {
        test_failure("Failed to read meta");
    }
    
    if (strcmp(result.name, original.name) != 0 ||
        result.size != original.size ||
        result.is_dir != original.is_dir) {
        test_failure("Meta data mismatch");
    }
    
    file_meta_free(&original);
    file_meta_free(&result);
    fclose(stream);
}

TEST(preflight_ok_roundtrip) {
    PreflightOk original = {.available_space = 123456789};
    
    FILE* stream = create_memstream();
    if (!stream) test_failure("Failed to create memory stream");
    
    if (write_preflight_ok(stream, &original) != 0) {
        test_failure("Failed to write preflight_ok");
    }
    
    rewind(stream);
    uint8_t msg_type;
    uint32_t msg_len;
    
    if (read_message_type(stream, &msg_type) != 0 ||
        read_message_length(stream, &msg_len) != 0) {
        test_failure("Failed to read message header");
    }
    
    if (msg_type != MSG_PREFLIGHT_OK) {
        test_failure("Wrong message type");
    }
    
    if (msg_len != 8) {
        test_failure("Wrong message length");
    }
    
    PreflightOk result;
    if (read_preflight_ok(stream, &result) != 0) {
        test_failure("Failed to read preflight_ok");
    }
    
    if (result.available_space != original.available_space) {
        test_failure("Available space mismatch");
    }
    
    fclose(stream);
}

TEST(preflight_fail_roundtrip) {
    PreflightFail original = {0};  // Initialize to zero first
    original.reason = strdup("Not enough space");
    if (!original.reason) test_failure("Failed to allocate reason");
    
    FILE* stream = create_memstream();
    if (!stream) test_failure("Failed to create memory stream");
    
    if (write_preflight_fail(stream, &original) != 0) {
        test_failure("Failed to write preflight_fail");
    }
    
    rewind(stream);
    uint8_t msg_type;
    uint32_t msg_len;
    
    if (read_message_type(stream, &msg_type) != 0 ||
        read_message_length(stream, &msg_len) != 0) {
        test_failure("Failed to read message header");
    }
    
    if (msg_type != MSG_PREFLIGHT_FAIL) {
        test_failure("Wrong message type");
    }
    
    if (msg_len != 4 + strlen(original.reason)) {
        test_failure("Wrong message length");
    }
    
    PreflightFail result = {0};
    if (read_preflight_fail(stream, &result) != 0) {
        test_failure("Failed to read preflight_fail");
    }
    
    if (strcmp(result.reason, original.reason) != 0) {
        test_failure("Reason mismatch");
    }
    
    preflight_fail_free(&original);
    preflight_fail_free(&result);
    fclose(stream);
}

TEST(transfer_start_roundtrip) {
    TransferStart original = {.file_size = 987654321};
    
    FILE* stream = create_memstream();
    if (!stream) test_failure("Failed to create memory stream");
    
    if (write_transfer_start(stream, &original) != 0) {
        test_failure("Failed to write transfer_start");
    }
    
    rewind(stream);
    uint8_t msg_type;
    uint32_t msg_len;
    
    if (read_message_type(stream, &msg_type) != 0 ||
        read_message_length(stream, &msg_len) != 0) {
        test_failure("Failed to read message header");
    }
    
    if (msg_type != MSG_TRANSFER_START) {
        test_failure("Wrong message type");
    }
    
    if (msg_len != 8) {
        test_failure("Wrong message length");
    }
    
    TransferStart result;
    if (read_transfer_start(stream, &result) != 0) {
        test_failure("Failed to read transfer_start");
    }
    
    if (result.file_size != original.file_size) {
        test_failure("File size mismatch");
    }
    
    fclose(stream);
}

TEST(transfer_result_roundtrip) {
    TransferResult original = {.ok = 1, .received_bytes = 555666777};
    
    FILE* stream = create_memstream();
    if (!stream) test_failure("Failed to create memory stream");
    
    if (write_transfer_result(stream, &original) != 0) {
        test_failure("Failed to write transfer_result");
    }
    
    rewind(stream);
    uint8_t msg_type;
    uint32_t msg_len;
    
    if (read_message_type(stream, &msg_type) != 0 ||
        read_message_length(stream, &msg_len) != 0) {
        test_failure("Failed to read message header");
    }
    
    if (msg_type != MSG_TRANSFER_RESULT) {
        test_failure("Wrong message type");
    }
    
    if (msg_len != 9) {
        test_failure("Wrong message length");
    }
    
    TransferResult result;
    if (read_transfer_result(stream, &result) != 0) {
        test_failure("Failed to read transfer_result");
    }
    
    if (result.ok != original.ok ||
        result.received_bytes != original.received_bytes) {
        test_failure("Transfer result mismatch");
    }
    
    fclose(stream);
}

TEST(directory_meta) {
    FileMeta dir_meta = {0};  // Initialize to zero first
    dir_meta.name = strdup("my_folder");
    if (!dir_meta.name) test_failure("Failed to allocate name");
    dir_meta.size = 0;
    dir_meta.is_dir = 1;
    
    FILE* stream = create_memstream();
    if (!stream) test_failure("Failed to create memory stream");
    
    if (write_meta(stream, &dir_meta) != 0) {
        test_failure("Failed to write directory meta");
    }
    
    rewind(stream);
    uint8_t msg_type;
    uint32_t msg_len;
    
    if (read_message_type(stream, &msg_type) != 0 ||
        read_message_length(stream, &msg_len) != 0) {
        test_failure("Failed to read message header");
    }
    
    FileMeta result = {0};
    if (read_meta(stream, &result) != 0) {
        test_failure("Failed to read directory meta");
    }
    
    if (strcmp(result.name, dir_meta.name) != 0 ||
        result.size != 0 ||
        !result.is_dir) {
        test_failure("Directory meta mismatch");
    }
    
    file_meta_free(&dir_meta);
    file_meta_free(&result);
    fclose(stream);
}

int main(void) {
    printf("Running protocol module tests...\n");
    
    run_meta_roundtrip();
    run_preflight_ok_roundtrip();
    run_preflight_fail_roundtrip();
    run_transfer_start_roundtrip();
    run_transfer_result_roundtrip();
    run_directory_meta();
    
    printf("All tests passed!\n");
    return 0;
}
