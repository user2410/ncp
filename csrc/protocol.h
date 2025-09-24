#ifndef NCP_PROTOCOL_H
#define NCP_PROTOCOL_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Message types
#define MSG_META           1  // FileMeta message
#define MSG_PREFLIGHT_OK   2  // PreflightOK response to FileMeta
#define MSG_PREFLIGHT_FAIL 3  // PreflightFail response to FileMeta
#define MSG_TRANSFER_START 4  // TransferStart message
#define MSG_TRANSFER_RESULT 5  // TransferResult message

// Protocol structures
typedef struct {
    char* name;          // Dynamically allocated
    uint64_t size;
    int is_dir;
    uint8_t overwrite_mode;  // 0: ask, 1: yes, 2: no
} FileMeta;

typedef struct {
    uint64_t available_space;
} PreflightOk;

typedef struct {
    char* reason;        // Dynamically allocated
} PreflightFail;

typedef struct {
    uint64_t file_size;
} TransferStart;

typedef struct {
    int ok;
    uint64_t received_bytes;
} TransferResult;

// Memory management functions
void file_meta_free(FileMeta* meta);
void preflight_fail_free(PreflightFail* msg);

// Write functions
int write_meta(FILE* writer, const FileMeta* meta);
int write_preflight_ok(FILE* writer, const PreflightOk* msg);
int write_preflight_fail(FILE* writer, const PreflightFail* msg);
int write_transfer_start(FILE* writer, const TransferStart* msg);
int write_transfer_result(FILE* writer, const TransferResult* msg);
int write_raw_bytes(FILE* writer, const uint8_t* data, size_t len);

// Read functions
int read_meta(FILE* reader, FileMeta* meta);
int read_preflight_ok(FILE* reader, PreflightOk* msg);
int read_preflight_fail(FILE* reader, PreflightFail* msg);
int read_transfer_start(FILE* reader, TransferStart* msg);
int read_transfer_result(FILE* reader, TransferResult* msg);
int read_message_type(FILE* reader, uint8_t* type);
int read_message_length(FILE* reader, uint32_t* length);
int read_exact_bytes(FILE* reader, uint8_t* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NCP_PROTOCOL_H */
