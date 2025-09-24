#ifndef NCP_SEND_H
#define NCP_SEND_H

#include "recv.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Execute sender in connect mode
void ncp_execute_send(const char* host, uint16_t port,
                    const char* src_path, uint32_t retries,
                    OverwriteMode overwrite_mode);

// Execute sender in listen mode
void ncp_execute_send_listen(uint16_t port, const char* src_path,
                          OverwriteMode overwrite_mode);

#ifdef __cplusplus
}
#endif

#endif // NCP_SEND_H
