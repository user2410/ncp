#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>  // for htonl, ntohl

// Helper functions for byte order conversion
static uint64_t htonll(uint64_t x) {
    if (htonl(1) == 1) return x;  // If we're on a big-endian system
    return ((uint64_t)htonl(x & 0xFFFFFFFF) << 32) | htonl(x >> 32);
}

static uint64_t ntohll(uint64_t x) {
    return htonll(x);  // Network to host is the same operation as host to network
}

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
    uint32_t len = 8 + 1 + 4 + name_len;
    uint32_t len_be = htonl(len);
    
    uint8_t type = MSG_META;
    uint64_t size_be = htonll(meta->size);
    uint8_t is_dir = meta->is_dir ? 1 : 0;
    
    if (fwrite(&type, 1, 1, writer) != 1) return -1;
    if (fwrite(&len_be, 4, 1, writer) != 1) return -1;
    if (fwrite(&size_be, 8, 1, writer) != 1) return -1;
    if (fwrite(&is_dir, 1, 1, writer) != 1) return -1;
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
    
    if (fread(&size_be, 8, 1, reader) != 1) return -1;
    if (fread(&is_dir, 1, 1, reader) != 1) return -1;
    
    meta->size = ntohll(size_be);
    meta->is_dir = is_dir != 0;
    
    if (read_string(reader, &meta->name) != 0) return -1;
    
    return 0;
}

int write_preflight_ok(FILE* writer, const PreflightOk* msg) {
    if (!writer || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    uint8_t type = MSG_PREFLIGHT_OK;
    uint32_t len = 8;
    uint32_t len_be = htonl(len);
    uint64_t space_be = htonll(msg->available_space);
    
    if (fwrite(&type, 1, 1, writer) != 1) return -1;
    if (fwrite(&len_be, 4, 1, writer) != 1) return -1;
    if (fwrite(&space_be, 8, 1, writer) != 1) return -1;
    
    fflush(writer);
    return 0;
}

int read_preflight_ok(FILE* reader, PreflightOk* msg) {
    if (!reader || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    uint64_t space_be;
    if (fread(&space_be, 8, 1, reader) != 1) return -1;
    
    msg->available_space = ntohll(space_be);
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
    uint64_t size_be = htonll(msg->file_size);
    
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
    
    msg->file_size = ntohll(size_be);
    return 0;
}

int write_transfer_result(FILE* writer, const TransferResult* msg) {
    if (!writer || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    uint8_t type = MSG_TRANSFER_RESULT;
    uint32_t len = 9;
    uint32_t len_be = htonl(len);
    uint8_t ok = msg->ok ? 1 : 0;
    uint64_t bytes_be = htonll(msg->received_bytes);
    
    if (fwrite(&type, 1, 1, writer) != 1) return -1;
    if (fwrite(&len_be, 4, 1, writer) != 1) return -1;
    if (fwrite(&ok, 1, 1, writer) != 1) return -1;
    if (fwrite(&bytes_be, 8, 1, writer) != 1) return -1;
    
    fflush(writer);
    return 0;
}

int read_transfer_result(FILE* reader, TransferResult* msg) {
    if (!reader || !msg) {
        errno = EINVAL;
        return -1;
    }
    
    uint8_t ok;
    uint64_t bytes_be;
    
    if (fread(&ok, 1, 1, reader) != 1) return -1;
    if (fread(&bytes_be, 8, 1, reader) != 1) return -1;
    
    msg->ok = ok != 0;
    msg->received_bytes = ntohll(bytes_be);
    return 0;
}

int read_message_type(FILE* reader, uint8_t* type) {
    if (!reader || !type) {
        errno = EINVAL;
        return -1;
    }
    
    return fread(type, 1, 1, reader) == 1 ? 0 : -1;
}

int read_message_length(FILE* reader, uint32_t* length) {
    if (!reader || !length) {
        errno = EINVAL;
        return -1;
    }
    
    uint32_t len_be;
    if (fread(&len_be, 4, 1, reader) != 1) return -1;
    
    *length = ntohl(len_be);
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
