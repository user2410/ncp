#include "protocol.hpp"
#include <stdexcept>
#include <cstring>

namespace ncp {

// Helper functions for byte order conversion
static uint32_t to_be32(uint32_t val) {
    return __builtin_bswap32(val);
}

static uint64_t to_be64(uint64_t val) {
    return __builtin_bswap64(val);
}

static uint32_t from_be32(uint32_t val) {
    return __builtin_bswap32(val);
}

static uint64_t from_be64(uint64_t val) {
    return __builtin_bswap64(val);
}

void write_meta(std::ostream& writer, const FileMeta& meta) {
    uint32_t name_len = meta.name.size();
    uint32_t len = 8 + 1 + 4 + name_len;
    
    writer.put(MSG_META);
    uint32_t len_be = to_be32(len);
    writer.write(reinterpret_cast<const char*>(&len_be), 4);
    
    uint64_t size_be = to_be64(meta.size);
    writer.write(reinterpret_cast<const char*>(&size_be), 8);
    
    writer.put(meta.is_dir ? 1 : 0);
    
    uint32_t name_len_be = to_be32(name_len);
    writer.write(reinterpret_cast<const char*>(&name_len_be), 4);
    writer.write(meta.name.c_str(), name_len);
    
    writer.flush();
}

FileMeta read_meta(std::istream& reader) {
    FileMeta meta;
    
    uint64_t size_be;
    reader.read(reinterpret_cast<char*>(&size_be), 8);
    meta.size = from_be64(size_be);
    
    char is_dir_byte;
    reader.read(&is_dir_byte, 1);
    meta.is_dir = (is_dir_byte != 0);
    
    uint32_t name_len_be;
    reader.read(reinterpret_cast<char*>(&name_len_be), 4);
    uint32_t name_len = from_be32(name_len_be);
    
    std::vector<char> name_buf(name_len);
    reader.read(name_buf.data(), name_len);
    meta.name = std::string(name_buf.begin(), name_buf.end());
    
    return meta;
}

void write_preflight_ok(std::ostream& writer, const PreflightOk& msg) {
    writer.put(MSG_PREFLIGHT_OK);
    uint32_t len_be = to_be32(8);
    writer.write(reinterpret_cast<const char*>(&len_be), 4);
    
    uint64_t space_be = to_be64(msg.available_space);
    writer.write(reinterpret_cast<const char*>(&space_be), 8);
    
    writer.flush();
}

PreflightOk read_preflight_ok(std::istream& reader) {
    PreflightOk msg;
    
    uint64_t space_be;
    reader.read(reinterpret_cast<char*>(&space_be), 8);
    msg.available_space = from_be64(space_be);
    
    return msg;
}

void write_preflight_fail(std::ostream& writer, const PreflightFail& msg) {
    uint32_t reason_len = msg.reason.size();
    uint32_t len = 4 + reason_len;
    
    writer.put(MSG_PREFLIGHT_FAIL);
    uint32_t len_be = to_be32(len);
    writer.write(reinterpret_cast<const char*>(&len_be), 4);
    
    uint32_t reason_len_be = to_be32(reason_len);
    writer.write(reinterpret_cast<const char*>(&reason_len_be), 4);
    writer.write(msg.reason.c_str(), reason_len);
    
    writer.flush();
}

PreflightFail read_preflight_fail(std::istream& reader) {
    PreflightFail msg;
    
    uint32_t reason_len_be;
    reader.read(reinterpret_cast<char*>(&reason_len_be), 4);
    uint32_t reason_len = from_be32(reason_len_be);
    
    std::vector<char> reason_buf(reason_len);
    reader.read(reason_buf.data(), reason_len);
    msg.reason = std::string(reason_buf.begin(), reason_buf.end());
    
    return msg;
}

void write_transfer_start(std::ostream& writer, const TransferStart& msg) {
    writer.put(MSG_TRANSFER_START);
    uint32_t len_be = to_be32(8);
    writer.write(reinterpret_cast<const char*>(&len_be), 4);
    
    uint64_t size_be = to_be64(msg.file_size);
    writer.write(reinterpret_cast<const char*>(&size_be), 8);
    
    writer.flush();
}

TransferStart read_transfer_start(std::istream& reader) {
    TransferStart msg;
    
    uint64_t size_be;
    reader.read(reinterpret_cast<char*>(&size_be), 8);
    msg.file_size = from_be64(size_be);
    
    return msg;
}

void write_transfer_result(std::ostream& writer, const TransferResult& msg) {
    writer.put(MSG_TRANSFER_RESULT);
    uint32_t len_be = to_be32(9);
    writer.write(reinterpret_cast<const char*>(&len_be), 4);
    
    writer.put(msg.ok ? 1 : 0);
    
    uint64_t bytes_be = to_be64(msg.received_bytes);
    writer.write(reinterpret_cast<const char*>(&bytes_be), 8);
    
    writer.flush();
}

TransferResult read_transfer_result(std::istream& reader) {
    TransferResult msg;
    
    char ok_byte;
    reader.read(&ok_byte, 1);
    msg.ok = (ok_byte != 0);
    
    uint64_t bytes_be;
    reader.read(reinterpret_cast<char*>(&bytes_be), 8);
    msg.received_bytes = from_be64(bytes_be);
    
    return msg;
}

uint8_t read_message_type(std::istream& reader) {
    char type;
    reader.read(&type, 1);
    return static_cast<uint8_t>(type);
}

uint32_t read_message_length(std::istream& reader) {
    uint32_t len_be;
    reader.read(reinterpret_cast<char*>(&len_be), 4);
    return from_be32(len_be);
}

void write_raw_bytes(std::ostream& writer, const std::vector<uint8_t>& data) {
    writer.write(reinterpret_cast<const char*>(data.data()), data.size());
    writer.flush();
}

void read_exact_bytes(std::istream& reader, std::vector<uint8_t>& buf) {
    reader.read(reinterpret_cast<char*>(buf.data()), buf.size());
}

} // namespace ncp