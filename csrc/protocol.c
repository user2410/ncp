#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define my_htonll(x) OSSwapHostToBigInt64(x)
#define my_ntohll(x) OSSwapBigToHostInt64(x)
#else
#include <arpa/inet.h>  // for htonl, ntohl

// Helper functions for byte order conversion
static uint64_t my_htonll(uint64_t x) {
    if (htonl(1) == 1) return x;  // If we're on a big-endian system
    return ((uint64_t)htonl(x & 0xFFFFFFFF) << 32) | htonl(x >> 32);
}

static uint64_t my_ntohll(uint64_t x) {
    return my_htonll(x);  // Network to host is the same operation as host to network
}
#endif

// Memory management functions
void file_meta_free(FileMeta* meta) {
    if (!meta) return;
    free(meta->name);
    meta->name = NULL;
}

void preflight_fail_free(PreflightFail* msg) {
    if (!msg) return;
    free(msg->reason);
    msg->reason = NULL;
}

// Helper function to write a string with length prefix
static int write_string(FILE* writer, const char* str) {
    if (!str) {
        uint32_t zero = 0;
        if (fwrite(&zero, 4, 1, writer) != 1) return -1;
        return 0;
    }
    
    uint32_t len = strlen(str);
    uint32_t len_be = htonl(len);
    
    if (fwrite(&len_be, 4, 1, writer) != 1) return -1;
    if (len > 0 && fwrite(str, 1, len, writer) != len) return -1;
    
    return 0;
}

// Helper function to read a string with length prefix
static int read_string(FILE* reader, char** str) {
    uint32_t len_be;
    if (fread(&len_be, 4, 1, reader) != 1) return -1;
    
    uint32_t len = ntohl(len_be);
    if (len == 0) {
        *str = NULL;
        return 0;
    }
    
    char* buf = malloc(len + 1);
    if (!buf) return -1;
    
    if (fread(buf, 1, len, reader) != len) {
        free(buf);
        return -1;
    }
    
    buf[len] = '\0';
    *str = buf;
    return 0;
}

int write_meta(FILE* writer, const FileMeta* meta) {
    if (!writer || !meta) {
        errno = EINVAL;
        return -1;
    }
    
    uint32_t name_len = meta->name ? strlen(meta->name) : 0;
    uint32_t len = 8 + 1 + 1 + 4 + name_len;  // size(8) + is_dir(1) + overwrite_mode(1) + name_len(4) + name
    uint32_t len_be = htonl(len);
    
    uint8_t type = MSG_META;
    uint64_t size_be = my_htonll(meta->size);
    uint8_t is_dir = meta->is_dir ? 1 : 0;
    uint8_t overwrite = meta->overwrite_mode;
    
    if (fwrite(&type, 1, 1, writer) != 1) return -1;
    if (fwrite(&len_be, 4, 1, writer) != 1) return -1;
    if (fwrite(&size_be, 8, 1, writer) != 1) return -1;
    if (fwrite(&is_dir, 1, 1, writer) != 1) return -1;
    if (fwrite(&overwrite, 1, 1, writer) != 1) return -1;
    if (write_string(writer, meta->name) != 0) return -1;
    
    fflush(writer);
    return 0;
}

int read_meta(FILE* reader, FileMeta* meta) {
    if (!reader || !meta) {
        errno = EINVAL;
        return -1;
    }
    
    uint64_t size_be;
    uint8_t is_dir;
    uint8_t overwrite;
    
    if (fread(&size_be, 8, 1, reader) != 1) return -1;
    if (fread(&is_dir, 1, 1, reader) != 1) return -1;
    if (fread(&overwrite, 1, 1, reader) != 1) return -1;
    
    meta->size = my_ntohll(size_be);
    meta->is_dir = is_dir != 0;
    meta->overwrite_mode = overwrite;
    
    if (read_string(reader, &meta->name) != 0) return -1;
    
    return 0;
}

int write_preflight_ok(FILE* writer, const PreflightOk* msg) {
    if (!writer || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    fprintf(stderr, "DEBUG: Protocol: Writing PreflightOK message (space=%llu)\n", 
            (unsigned long long)msg->available_space);
    uint8_t type = MSG_PREFLIGHT_OK;
    uint32_t len = 8;
    uint32_t len_be = htonl(len);
    uint64_t space_be = my_htonll(msg->available_space);
    
    fprintf(stderr, "DEBUG: Protocol: Writing type=%d, len=%u, space_be=0x%016llx\n", 
            type, len, (unsigned long long)space_be);
    
    if (fwrite(&type, 1, 1, writer) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to write type (errno: %d)\n", errno);
        return -1;
    }
    if (fwrite(&len_be, 4, 1, writer) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to write length (errno: %d)\n", errno);
        return -1;
    }
    if (fwrite(&space_be, 8, 1, writer) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to write space (errno: %d)\n", errno);
        return -1;
    }
    
    fprintf(stderr, "DEBUG: Protocol: PreflightOK message written, flushing\n");
    if (fflush(writer) != 0) {
        fprintf(stderr, "DEBUG: Protocol: Failed to flush PreflightOK (errno: %d)\n", errno);
        return -1;
    }
    fprintf(stderr, "DEBUG: Protocol: PreflightOK flushed successfully\n");
    return 0;
}

int read_preflight_ok(FILE* reader, PreflightOk* msg) {
    if (!reader || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    uint64_t space_be;
    if (fread(&space_be, 8, 1, reader) != 1) return -1;
    
    msg->available_space = my_ntohll(space_be);
    return 0;
}

int write_preflight_fail(FILE* writer, const PreflightFail* msg) {
    if (!writer || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    uint32_t reason_len = msg->reason ? strlen(msg->reason) : 0;
    uint32_t len = 4 + reason_len;
    uint32_t len_be = htonl(len);
    
    uint8_t type = MSG_PREFLIGHT_FAIL;
    
    if (fwrite(&type, 1, 1, writer) != 1) return -1;
    if (fwrite(&len_be, 4, 1, writer) != 1) return -1;
    if (write_string(writer, msg->reason) != 0) return -1;
    
    fflush(writer);
    return 0;
}

int read_preflight_fail(FILE* reader, PreflightFail* msg) {
    if (!reader || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    return read_string(reader, &msg->reason);
}

int write_transfer_start(FILE* writer, const TransferStart* msg) {
    if (!writer || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    uint8_t type = MSG_TRANSFER_START;
    uint32_t len = 8;
    uint32_t len_be = htonl(len);
    uint64_t size_be = my_htonll(msg->file_size);
    
    if (fwrite(&type, 1, 1, writer) != 1) return -1;
    if (fwrite(&len_be, 4, 1, writer) != 1) return -1;
    if (fwrite(&size_be, 8, 1, writer) != 1) return -1;
    
    fflush(writer);
    return 0;
}

int read_transfer_start(FILE* reader, TransferStart* msg) {
    if (!reader || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    uint64_t size_be;
    if (fread(&size_be, 8, 1, reader) != 1) return -1;
    
    msg->file_size = my_ntohll(size_be);
    return 0;
}

int write_transfer_result(FILE* writer, const TransferResult* msg) {
    if (!writer || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    fprintf(stderr, "DEBUG: Protocol: Writing TransferResult message (ok=%d, bytes=%llu)\n", 
            msg->ok, (unsigned long long)msg->received_bytes);
    uint8_t type = MSG_TRANSFER_RESULT;
    uint32_t len = 9;
    uint32_t len_be = htonl(len);
    uint8_t ok = msg->ok ? 1 : 0;
    uint64_t bytes_be = my_htonll(msg->received_bytes);
    
    fprintf(stderr, "DEBUG: Protocol: Writing type=%d, len=%u, ok=%d, bytes_be=0x%016llx\n", 
            type, len, ok, (unsigned long long)bytes_be);
    
    if (fwrite(&type, 1, 1, writer) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to write type (errno: %d)\n", errno);
        return -1;
    }
    if (fwrite(&len_be, 4, 1, writer) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to write length (errno: %d)\n", errno);
        return -1;
    }
    if (fwrite(&ok, 1, 1, writer) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to write ok flag (errno: %d)\n", errno);
        return -1;
    }
    if (fwrite(&bytes_be, 8, 1, writer) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to write bytes (errno: %d)\n", errno);
        return -1;
    }
    
    fprintf(stderr, "DEBUG: Protocol: TransferResult written, flushing\n");
    if (fflush(writer) != 0) {
        fprintf(stderr, "DEBUG: Protocol: Failed to flush TransferResult (errno: %d)\n", errno);
        return -1;
    }
    fprintf(stderr, "DEBUG: Protocol: TransferResult flushed successfully\n");
    return 0;
}

// Helper function to read transfer result with full message handling
int read_transfer_result_full(FILE* reader, TransferResult* msg) {
    if (!reader || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    fprintf(stderr, "DEBUG: Protocol: Reading TransferResult message (full)\n");
    
    uint8_t msg_type;
    if (read_message_type(reader, &msg_type) != 0) {
        fprintf(stderr, "DEBUG: Protocol: Failed to read message type (errno: %d)\n", errno);
        return -1;
    }
    fprintf(stderr, "DEBUG: Protocol: Read message type: %d\n", msg_type);
    
    if (msg_type != MSG_TRANSFER_RESULT) {
        fprintf(stderr, "DEBUG: Protocol: Expected MSG_TRANSFER_RESULT (%d), got %d\n", 
                MSG_TRANSFER_RESULT, msg_type);
        errno = EPROTO;
        return -1;
    }
    
    uint32_t msg_len;
    if (read_message_length(reader, &msg_len) != 0) {
        fprintf(stderr, "DEBUG: Protocol: Failed to read message length (errno: %d)\n", errno);
        return -1;
    }
    fprintf(stderr, "DEBUG: Protocol: Read message length: %u\n", msg_len);
    
    return read_transfer_result_payload(reader, msg);
}

// Helper function to read just the payload part
int read_transfer_result_payload(FILE* reader, TransferResult* msg) {
    if (!reader || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    fprintf(stderr, "DEBUG: Protocol: Reading TransferResult payload\n");
    uint8_t ok;
    uint64_t bytes_be;
    
    // Read payload only - header should already be read by caller
    if (fread(&ok, 1, 1, reader) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to read ok flag (errno: %d, ferror: %d, feof: %d)\n", 
                errno, ferror(reader), feof(reader));
        return -1;
    }
    fprintf(stderr, "DEBUG: Protocol: Read ok flag: %d\n", ok);
    
    if (fread(&bytes_be, 8, 1, reader) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to read bytes (errno: %d, ferror: %d, feof: %d)\n", 
                errno, ferror(reader), feof(reader));
        return -1;
    }
    fprintf(stderr, "DEBUG: Protocol: Read bytes value (raw: 0x%016llx)\n", 
            (unsigned long long)bytes_be);
    
    msg->ok = ok != 0;
    msg->received_bytes = my_ntohll(bytes_be);
    fprintf(stderr, "DEBUG: Protocol: TransferResult payload read successfully (ok=%d, bytes=%llu)\n",
            msg->ok, (unsigned long long)msg->received_bytes);
    return 0;
}

int read_transfer_result(FILE* reader, TransferResult* msg) {
    return read_transfer_result_payload(reader, msg);
}

int read_message_type(FILE* reader, uint8_t* type) {
    if (!reader || !type) {
        errno = EINVAL;
        return -1;
    }
    
    fprintf(stderr, "DEBUG: Protocol: Reading message type\n");
    size_t bytes_read = fread(type, 1, 1, reader);
    if (bytes_read != 1) {
        if (feof(reader)) {
            fprintf(stderr, "DEBUG: Protocol: End of stream reached (connection closed)\n");
        } else {
            fprintf(stderr, "DEBUG: Protocol: Failed to read message type (bytes_read=%zu, errno=%d, ferror=%d, feof=%d)\n", 
                    bytes_read, errno, ferror(reader), feof(reader));
        }
        return -1;
    }
    fprintf(stderr, "DEBUG: Protocol: Read message type: %d\n", *type);
    return 0;
}

int read_message_length(FILE* reader, uint32_t* length) {
    if (!reader || !length) {
        errno = EINVAL;
        return -1;
    }
    
    fprintf(stderr, "DEBUG: Protocol: Reading message length\n");
    uint32_t len_be;
    if (fread(&len_be, 4, 1, reader) != 1) {
        fprintf(stderr, "DEBUG: Protocol: Failed to read message length (errno=%d, ferror=%d, feof=%d)\n", 
                errno, ferror(reader), feof(reader));
        return -1;
    }
    
    *length = ntohl(len_be);
    fprintf(stderr, "DEBUG: Protocol: Read message length: %u (raw: 0x%08x)\n", *length, len_be);
    return 0;
}

int write_raw_bytes(FILE* writer, const uint8_t* data, size_t len) {
    if (!writer || (!data && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    
    if (len > 0 && fwrite(data, 1, len, writer) != len) return -1;
    fflush(writer);
    return 0;
}

int read_exact_bytes(FILE* reader, uint8_t* buf, size_t len) {
    if (!reader || (!buf && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    
    return len > 0 && fread(buf, 1, len, reader) != len ? -1 : 0;
}
