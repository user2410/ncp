#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

namespace ncp {

// Message types
constexpr uint8_t MSG_META = 1;
constexpr uint8_t MSG_PREFLIGHT_OK = 2;
constexpr uint8_t MSG_PREFLIGHT_FAIL = 3;
constexpr uint8_t MSG_TRANSFER_START = 4;
constexpr uint8_t MSG_TRANSFER_RESULT = 5;

// Protocol structures
struct FileMeta {
    std::string name;
    uint64_t size;
    bool is_dir;
};

struct PreflightOk {
    uint64_t available_space;
};

struct PreflightFail {
    std::string reason;
};

struct TransferStart {
    uint64_t file_size;
};

struct TransferResult {
    bool ok;
    uint64_t received_bytes;
};

// Write functions
void write_meta(std::ostream& writer, const FileMeta& meta);
void write_preflight_ok(std::ostream& writer, const PreflightOk& msg);
void write_preflight_fail(std::ostream& writer, const PreflightFail& msg);
void write_transfer_start(std::ostream& writer, const TransferStart& msg);
void write_transfer_result(std::ostream& writer, const TransferResult& msg);
void write_raw_bytes(std::ostream& writer, const std::vector<uint8_t>& data);

// Read functions
FileMeta read_meta(std::istream& reader);
PreflightOk read_preflight_ok(std::istream& reader);
PreflightFail read_preflight_fail(std::istream& reader);
TransferStart read_transfer_start(std::istream& reader);
TransferResult read_transfer_result(std::istream& reader);
uint8_t read_message_type(std::istream& reader);
uint32_t read_message_length(std::istream& reader);
void read_exact_bytes(std::istream& reader, std::vector<uint8_t>& buf);

} // namespace ncp