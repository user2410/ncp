#include "directory.hpp"
#include <algorithm>
#include <stdexcept>

namespace ncp {

static void walk_recursive(const std::filesystem::path& root, 
                          const std::filesystem::path& current, 
                          std::vector<FileEntry>& entries) {
    std::error_code ec;
    auto status = std::filesystem::status(current, ec);
    if (ec) {
        throw std::runtime_error("Failed to get file status: " + ec.message());
    }
    
    auto relative_path = std::filesystem::relative(current, root, ec);
    if (ec) {
        throw std::runtime_error("Failed to create relative path: " + ec.message());
    }
    
    if (std::filesystem::is_directory(status)) {
        // Add directory entry
        entries.push_back({
            current,
            relative_path,
            true,
            0
        });
        
        // Recursively process directory contents
        for (const auto& entry : std::filesystem::directory_iterator(current, ec)) {
            if (ec) {
                throw std::runtime_error("Failed to iterate directory: " + ec.message());
            }
            walk_recursive(root, entry.path(), entries);
        }
    } else {
        // Add file entry
        auto size = std::filesystem::file_size(current, ec);
        if (ec) {
            throw std::runtime_error("Failed to get file size: " + ec.message());
        }
        
        entries.push_back({
            current,
            relative_path,
            false,
            size
        });
    }
}

std::vector<FileEntry> walk_directory(const std::filesystem::path& root) {
    std::vector<FileEntry> entries;
    walk_recursive(root, root, entries);
    
    // Sort entries: directories first, then files, both alphabetically
    std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir > b.is_dir; // directories first
        }
        return a.relative_path < b.relative_path;
    });
    
    return entries;
}

uint64_t calculate_total_size(const std::vector<FileEntry>& entries) {
    uint64_t total = 0;
    for (const auto& entry : entries) {
        if (!entry.is_dir) {
            total += entry.size;
        }
    }
    return total;
}

} // namespace ncp