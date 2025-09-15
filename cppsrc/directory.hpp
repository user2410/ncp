#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace ncp {

struct FileEntry {
    std::filesystem::path path;
    std::filesystem::path relative_path;
    bool is_dir;
    uint64_t size;
};

// Recursively walk directory and collect all files and directories
std::vector<FileEntry> walk_directory(const std::filesystem::path& root);

// Calculate total size of all files in entries
uint64_t calculate_total_size(const std::vector<FileEntry>& entries);

} // namespace ncp