#include "logging.h"

// Global verbosity level
static int g_verbosity = LOG_LEVEL_NONE;

void set_verbosity(int level) {
    g_verbosity = level;
}

int get_verbosity(void) {
    return g_verbosity;
}