#pragma once

#include "recv.hpp"
#include <string>
#include <filesystem>
#include <cstdint>

namespace ncp {

// Execute sender in connect mode
void execute_send(const std::string& host, uint16_t port,
                  const std::filesystem::path& src, uint32_t retries,
                  OverwriteMode overwrite_mode);

// Execute sender in listen mode
void execute_send_listen(uint16_t port, const std::filesystem::path& src,
                        OverwriteMode overwrite_mode);

} // namespace ncp