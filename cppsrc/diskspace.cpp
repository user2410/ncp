#include "diskspace.hpp"
#include <stdexcept>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/statvfs.h>
#endif

namespace ncp {

uint64_t get_available_space(const std::filesystem::path& path) {
    std::error_code ec;
    
    // If path doesn't exist, check its parent directory
    auto check_path = path;
    if (!std::filesystem::exists(path, ec)) {
        auto parent = path.parent_path();
        if (parent.empty()) {
            check_path = ".";
        } else {
            check_path = parent;
        }
    }
    
#ifdef _WIN32
    ULARGE_INTEGER free_bytes_available;
    ULARGE_INTEGER total_number_of_bytes;
    ULARGE_INTEGER total_number_of_free_bytes;
    
    if (!GetDiskFreeSpaceExW(
        check_path.c_str(),
        &free_bytes_available,
        &total_number_of_bytes,
        &total_number_of_free_bytes)) {
        throw std::runtime_error("Failed to get disk space");
    }
    
    return free_bytes_available.QuadPart;
#else
    struct statvfs stat;
    if (statvfs(check_path.c_str(), &stat) != 0) {
        throw std::runtime_error("Failed to get disk space");
    }
    
    // Available space = available blocks * block size
    uint64_t available_space = static_cast<uint64_t>(stat.f_bavail) * static_cast<uint64_t>(stat.f_frsize);
    return available_space;
#endif
}

bool check_disk_space(const std::filesystem::path& path, uint64_t required_bytes) {
    uint64_t available = get_available_space(path);
    
    // Add 10% buffer for safety
    uint64_t buffer = required_bytes / 10;
    uint64_t required_with_buffer = required_bytes + buffer;
    
    // Check for overflow
    if (required_with_buffer < required_bytes) {
        required_with_buffer = UINT64_MAX;
    }
    
    return available >= required_with_buffer;
}

std::string format_bytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    const uint64_t threshold = 1024;
    
    if (bytes < threshold) {
        return std::to_string(bytes) + " B";
    }
    
    double size = static_cast<double>(bytes);
    size_t unit_index = 0;
    
    while (size >= threshold && unit_index < 4) {
        size /= threshold;
        unit_index++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << size << " " << units[unit_index];
    return oss.str();
}

} // namespace ncp