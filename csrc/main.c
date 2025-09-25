#define _POSIX_C_SOURCE 200809L
#include "send.h"
#include "recv.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define VERSION "0.1.0"

typedef struct {
    uint8_t verbose;
    enum { CMD_SEND, CMD_RECV } command_type;
    
    // Common args
    char* host;
    uint16_t port;
    OverwriteMode overwrite;
    int listen;
    char* src_or_dst;
    
    // Send-specific args
    uint32_t retries;
} Args;

static void print_help(void) {
    printf("ncp %s - Minimal file transfer over TCP\n\n", VERSION);
    printf("USAGE:\n");
    printf("    ncp [-v|-vv] send --host <HOST> --port <PORT> [OPTIONS] <SRC>\n");
    printf("    ncp [-v|-vv] send --listen --port <PORT> [OPTIONS] <SRC>\n");
    printf("    ncp [-v|-vv] recv --port <PORT> [OPTIONS] <DST>\n");
    printf("    ncp [-v|-vv] recv --host <HOST> --port <PORT> [OPTIONS] <DST>\n");
    printf("    ncp [-v|-vv] recv --listen --port <PORT> [OPTIONS] <DST>\n\n");
    printf("OPTIONS:\n");
    printf("    -v, -vv          Increase verbosity\n");
    printf("    --host <HOST>    Target/bind host (auto-enables connect mode for recv)\n");
    printf("    --port <PORT>    Port number\n");
    printf("    --listen, -l     Listen mode (send and recv)\n");
    printf("    --retries <N>    Retry attempts (send only, default: 3)\n");
    printf("    --overwrite <M>  Overwrite mode: ask, yes, no (default: ask)\n");
    printf("    -h, --help       Show this help\n");
}

static OverwriteMode parse_overwrite_mode(const char* mode) {
    if (strcmp(mode, "ask") == 0) return OVERWRITE_ASK;
    if (strcmp(mode, "yes") == 0) return OVERWRITE_YES;
    if (strcmp(mode, "no") == 0) return OVERWRITE_NO;
    fprintf(stderr, "Invalid overwrite mode: %s\n", mode);
    exit(1);
}

static Args parse_send_args(int argc, char* argv[], int start_idx) {
    Args args = {0};
    args.command_type = CMD_SEND;
    args.retries = 3;  // Default value
    args.overwrite = OVERWRITE_ASK;  // Default value
    int host_specified = 0;
    int src_specified = 0;
    
    for (int i = start_idx; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--host requires value\n");
                exit(1);
            }
            args.host = argv[i];
            host_specified = 1;
        } else if (strcmp(argv[i], "--port") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--port requires value\n");
                exit(1);
            }
            char* endptr;
            long port = strtol(argv[i], &endptr, 10);
            if (*endptr != '\0' || port <= 0 || port > 65535) {
                fprintf(stderr, "Invalid port number\n");
                exit(1);
            }
            args.port = (uint16_t)port;
        } else if (strcmp(argv[i], "--retries") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--retries requires value\n");
                exit(1);
            }
            char* endptr;
            long retries = strtol(argv[i], &endptr, 10);
            if (*endptr != '\0' || retries < 0) {
                fprintf(stderr, "Invalid retries value\n");
                exit(1);
            }
            args.retries = (uint32_t)retries;
        } else if (strcmp(argv[i], "--overwrite") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--overwrite requires value\n");
                exit(1);
            }
            args.overwrite = parse_overwrite_mode(argv[i]);
        } else if (strcmp(argv[i], "--listen") == 0 || strcmp(argv[i], "-l") == 0) {
            args.listen = 1;
        } else if (argv[i][0] != '-') {
            args.src_or_dst = argv[i];
            src_specified = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            exit(1);
        }
    }
    
    if (!args.listen && !host_specified) {
        fprintf(stderr, "--host required (or use --listen)\n");
        exit(1);
    }
    if (args.port == 0) {
        fprintf(stderr, "--port required\n");
        exit(1);
    }
    if (!src_specified) {
        fprintf(stderr, "source path required\n");
        exit(1);
    }
    
    return args;
}

static Args parse_recv_args(int argc, char* argv[], int start_idx) {
    Args args = {0};
    args.command_type = CMD_RECV;
    args.host = "0.0.0.0";  // Default value
    args.overwrite = OVERWRITE_ASK;  // Default value
    int host_specified = 0;
    int dst_specified = 0;
    
    for (int i = start_idx; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--host requires value\n");
                exit(1);
            }
            args.host = argv[i];
            host_specified = 1;
        } else if (strcmp(argv[i], "--port") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--port requires value\n");
                exit(1);
            }
            char* endptr;
            long port = strtol(argv[i], &endptr, 10);
            if (*endptr != '\0' || port <= 0 || port > 65535) {
                fprintf(stderr, "Invalid port number\n");
                exit(1);
            }
            args.port = (uint16_t)port;
        } else if (strcmp(argv[i], "--overwrite") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--overwrite requires value\n");
                exit(1);
            }
            args.overwrite = parse_overwrite_mode(argv[i]);
        } else if (strcmp(argv[i], "--listen") == 0 || strcmp(argv[i], "-l") == 0) {
            args.listen = 1;
        } else if (argv[i][0] != '-') {
            args.src_or_dst = argv[i];
            dst_specified = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            exit(1);
        }
    }
    
    // Logic: --listen forces listen mode, --host forces connect mode, default is listen
    if (!args.listen && !host_specified) {
        args.listen = 1;
    }
    
    if (args.port == 0) {
        fprintf(stderr, "--port required\n");
        exit(1);
    }
    if (!dst_specified) {
        fprintf(stderr, "destination path required\n");
        exit(1);
    }
    
    return args;
}

static Args parse_args(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ncp [send|recv] [options]\n");
        exit(1);
    }
    
    Args args = {0};
    int i = 1;
    
    // Parse global options
    while (i < argc && argv[i][0] == '-' && strcmp(argv[i], "--") != 0) {
        if (strcmp(argv[i], "-v") == 0) {
            args.verbose = 1;
        } else if (strcmp(argv[i], "-vv") == 0) {
            args.verbose = 2;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            exit(0);
        } else {
            break;
        }
        i++;
    }
    
    if (i >= argc) {
        fprintf(stderr, "Missing command\n");
        exit(1);
    }
    
    if (strcmp(argv[i], "send") == 0) {
        Args send_args = parse_send_args(argc, argv, i + 1);
        args.command_type = send_args.command_type;
        args.host = send_args.host;
        args.port = send_args.port;
        args.retries = send_args.retries;
        args.overwrite = send_args.overwrite;
        args.listen = send_args.listen;
        args.src_or_dst = send_args.src_or_dst;
    } else if (strcmp(argv[i], "recv") == 0) {
        Args recv_args = parse_recv_args(argc, argv, i + 1);
        args.command_type = recv_args.command_type;
        args.host = recv_args.host;
        args.port = recv_args.port;
        args.overwrite = recv_args.overwrite;
        args.listen = recv_args.listen;
        args.src_or_dst = recv_args.src_or_dst;
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[i]);
        exit(1);
    }
    
    return args;
}

int main(int argc, char* argv[]) {
    Args args = parse_args(argc, argv);
    
    // Set global verbosity level
    set_verbosity(args.verbose);
    
    if (args.verbose >= 1) {
        fprintf(stderr, "[INFO] Starting ncp with verbosity level %d\n", args.verbose);
    }
    
    int result = 0;
    
    if (args.command_type == CMD_SEND) {
        if (args.verbose >= 2) {
            if (args.listen) {
                fprintf(stderr, "[DEBUG] Executing send listen command: port %d -> %s\n",
                        args.port, args.src_or_dst);
            } else {
                fprintf(stderr, "[DEBUG] Executing send command: %s:%d -> %s\n",
                        args.host, args.port, args.src_or_dst);
            }
        }
        
        if (args.listen) {
            result = ncp_execute_send_listen(args.port, args.src_or_dst, args.overwrite);
        } else {
            result = ncp_execute_send(args.host, args.port, args.src_or_dst, args.retries, args.overwrite);
        }
    } else {
        if (args.verbose >= 2) {
            if (args.listen) {
                fprintf(stderr, "[DEBUG] Executing recv listen command: %s:%d -> %s\n",
                        args.host, args.port, args.src_or_dst);
            } else {
                fprintf(stderr, "[DEBUG] Executing recv connect command: %s:%d -> %s\n",
                        args.host, args.port, args.src_or_dst);
            }
        }
        
        if (args.listen) {
            result = recv_execute(args.host, args.port, args.src_or_dst, args.overwrite);
        } else {
            result = recv_execute_connect(args.host, args.port, args.src_or_dst, args.overwrite);
        }
    }
    
    if (result == 0 && args.verbose >= 1) {
        fprintf(stderr, "[INFO] Operation completed successfully\n");
    }
    
    return result;
}
