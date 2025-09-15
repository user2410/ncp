#pragma once

#include <string>
#include <filesystem>
#include <cstdint>

namespace ncp {

enum class OverwriteMode {
    Ask,
    Yes,
    No
};

// Execute receiver in listen mode
void execute(const std::string& host, uint16_t port, 
             const std::filesystem::path& dst, OverwriteMode overwrite_mode);

// Execute receiver in connect mode
void execute_connect(const std::string& host, uint16_t port,
                    const std::filesystem::path& dst, OverwriteMode overwrite_mode);

} // namespace ncp