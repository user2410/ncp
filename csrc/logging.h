#ifndef NCP_LOGGING_H
#define NCP_LOGGING_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Verbosity levels
#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_DEBUG 2

// Set global verbosity level
void set_verbosity(int level);

// Get current verbosity level
int get_verbosity(void);

// Debug logging macro
#define DEBUG_LOG(level, ...) do { \
    if (get_verbosity() >= level) { \
        fprintf(stderr, __VA_ARGS__); \
    } \
} while(0)

// Convenience macros
#define LOG_INFO(...) DEBUG_LOG(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DEBUG(...) DEBUG_LOG(LOG_LEVEL_DEBUG, __VA_ARGS__)

// Regular output (always shown)
#define LOG_OUTPUT(...) printf(__VA_ARGS__)

// Error output (always shown)
#define LOG_ERROR(...) fprintf(stderr, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* NCP_LOGGING_H */