#pragma once

#include <string>
#include <filesystem>
#include <cstdint>

namespace ncp {

// Get available disk space for a given path
uint64_t get_available_space(const std::filesystem::path& path);

// Check if there's enough disk space for a file transfer
bool check_disk_space(const std::filesystem::path& path, uint64_t required_bytes);

// Format bytes in human-readable format
std::string format_bytes(uint64_t bytes);

} // namespace ncp