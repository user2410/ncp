# ncp — Rust Simple Copy Protocol

Lightweight, fast CLI for copying files over TCP with a small, efficient protocol based on protobuf messages and a single TCP stream. Implemented in Rust for memory safety and performance.

---

## Table of Contents

- [ncp — Rust Simple Copy Protocol](#ncp--rust-simple-copy-protocol)
  - [Table of Contents](#table-of-contents)
  - [Goals \& Principles](#goals--principles)
  - [High-level overview](#high-level-overview)
  - [Protocol design](#protocol-design)
    - [Framing and wire format](#framing-and-wire-format)
    - [Protocol messages (Protobuf)](#protocol-messages-protobuf)
    - [Session lifecycle (summary)](#session-lifecycle-summary)
  - [State machine](#state-machine)
    - [Sender states](#sender-states)
    - [Receiver states](#receiver-states)
  - [Preflight checks, atomic write \& disk semantics](#preflight-checks-atomic-write--disk-semantics)
    - [Atomic write strategy](#atomic-write-strategy)
  - [Resume \& chunking strategy](#resume--chunking-strategy)
  - [Security \& TLS](#security--tls)
- [Exit codes \& error model](#exit-codes--error-model)
  - [Exit codes (process-level)](#exit-codes-process-level)
  - [Protocol error model](#protocol-error-model)
    - [Fatal errors (no automatic retries)](#fatal-errors-no-automatic-retries)
    - [Transient errors (retryable)](#transient-errors-retryable)
    - [Retry policy (configurable)](#retry-policy-configurable)
- [Wire format \& framing](#wire-format--framing)
  - [High-level rules](#high-level-rules)
    - [Frame format (control frames)](#frame-format-control-frames)
    - [Switching to raw data](#switching-to-raw-data)
- [Protobuf schema (proto3)](#protobuf-schema-proto3)
- [State machine (sequence \& diagrams)](#state-machine-sequence--diagrams)
  - [Sequence (single-file, no-resume)](#sequence-single-file-no-resume)
  - [Sequence (resume)](#sequence-resume)
- [CLI reference](#cli-reference)
  - [Basic usage examples](#basic-usage-examples)
  - [CLI flags (grouped)](#cli-flags-grouped)
    - [Common](#common)
    - [Send](#send)
    - [Receive](#receive)
- [Implementation notes (Rust)](#implementation-notes-rust)
  - [Recommended crates](#recommended-crates)
  - [Project layout](#project-layout)
  - [Protobuf build](#protobuf-build)
  - [Framing helpers](#framing-helpers)
  - [Raw-data transfer](#raw-data-transfer)
  - [Atomic write and fsync](#atomic-write-and-fsync)
  - [Handling large files](#handling-large-files)
  - [Signal handling](#signal-handling)
- [Tests \& validation checklist](#tests--validation-checklist)
  - [Unit tests](#unit-tests)
  - [Integration tests](#integration-tests)
  - [Fuzz tests](#fuzz-tests)
- [Operational notes](#operational-notes)
  - [Security](#security)
  - [NAT and firewalls](#nat-and-firewalls)
  - [Monitoring \& metrics](#monitoring--metrics)
  - [Logging](#logging)
- [Examples](#examples)
    - [Example: Minimal send (no TLS)](#example-minimal-send-no-tls)
    - [Example: TLS + resume](#example-tls--resume)
- [Roadmap \& development phases](#roadmap--development-phases)
- [Appendix: Quick design decisions \& rationale (summary)](#appendix-quick-design-decisions--rationale-summary)

---

## Goals & Principles

**Phase 1 - Minimal Version**:
- **Smallest binary**: absolute minimum dependencies, std library only where possible.
- **Working**: basic file transfer with checksum validation.
- **Safe**: Rust memory safety and atomic file handling.
- **Simple**: plain TCP, synchronous I/O, essential features only.

**Future Phases**:
- **Fast**: efficient async I/O, streaming checksums, configurable chunking.
- **Practical**: works over NAT and firewalls (single TCP connection by default).
- **Extensible**: protobuf messages and versioning for future features.
- **Secure**: TLS support for production use.

---

## High-level overview

- Single TCP connection is used for control messages (protobuf) and, after negotiation, for raw file bytes.
- The sender initiates connection and sends periodic `Probe` messages until receiver responds.
- The receiver validates the metadata, performs preflight checks, and replies with `PreflightResult`.
- If OK, the sender and receiver begin a raw-bytes data transfer for exactly `file_size` bytes. After data transfer, the receiver validates checksum and replies with the `TransferResult`.
- The receiver writes to a temporary file and renames atomically to the final filename only after checksum validation.

---

## Protocol design

### Framing and wire format

- All control messages are encoded using **Protocol Buffers (proto3)** with 4-byte big-endian length prefix.
- After negotiation, raw file bytes are streamed without additional framing.
- See [Wire format & framing](#wire-format--framing) section for detailed specifications.

### Protocol messages (Protobuf)

Key messages include: `Probe`, `Established`, `Meta`, `PreflightResult`, `TransferStart`, `TransferResult`, `Heartbeat`, `Error`.

See full `.proto` below.

### Session lifecycle (summary)

1. Sender connects and sends `Probe` (periodically until `Established`).
2. Receiver replies with `Established` (including a `session_id`, protocol version, capabilities).
3. Sender sends `Meta` (file metadata: name, size, mode, mtime, checksum algorithm + checksum).
4. Receiver runs preflight (permissions, free space, policies) and replies `PreflightResult` (`ok` or `fail` with reason).
5. If `ok`, Sender sends `TransferStart` (indicates transfer parameters). Immediately after that, raw bytes are streamed (exactly `size` bytes).
6. Receiver reads `size` bytes to a temporary file while computing streaming checksum.
7. Receiver sends `TransferResult` with success/failure and computed checksum.
8. Sender receives `TransferResult` and exits accordingly (or retries if configured).

---

## State machine

### Sender states

- `IDLE` — initial.
- `CONNECTING` — TCP connect attempt.
- `PROBING` — send `Probe` repeatedly until `Established` or timeout.
- `ESTABLISHED` — control channel active.
- `SENDING_META` — send `Meta` and wait `PreflightResult`.
- `WAIT_PREFLIGHT` — waiting for `PreflightResult`.
- `TRANSFER_START` — send `TransferStart` then stream raw bytes.
- `TRANSFERRING` — streaming raw bytes.
- `WAIT_TRANSFER_RESULT` — waiting for `TransferResult`.
- `SUCCESS` / `FAIL` — terminal states.

### Receiver states

- `LISTEN` — listening on socket.
- `WAIT_PROBE` — accept connection and wait first `Probe`.
- `ESTABLISHED` — send `Established` reply.
- `WAIT_META` — get `Meta`.
- `PREFLIGHT` — perform preflight checks.
- `READY` — send `PreflightResult.ok=true`.
- `RECEIVING` — read raw bytes into temp file.
- `VALIDATING` — compute and compare checksum, rename temp.
- `DONE` / `FAIL` — terminal.

---

## Preflight checks, atomic write & disk semantics

Receiver must perform the following checks before signalling OK:

1. **Destination directory exists and is writable**: attempt a small test write in the destination directory or check permissions.
2. **Available space**: ensure `available_space >= file.size + reserve` (reserve configurable).
3. **Policy checks**: e.g., max file size limit, user-supplied policies, overwrite rules.
4. **File name sanitization**: normalize path components to avoid directory traversal — **reject any path that attempts to escape destination root**.

### Atomic write strategy

- Write incoming bytes to a temporary file in the **same filesystem** as the destination: `.<filename>.ncp.<session_id>.part`.
- Ensure `fsync` after write completes if `--fsync` is enabled.
- After validating checksum, set file permissions/mtime, then rename to final filename using `std::fs::rename` (atomic within same FS).
- If validation fails, remove temp file and return a failure code.

---

## Resume & chunking strategy

**Two modes** supported (negotiated via `capabilities`):

1. **Simple (no-resume)**: single raw-bytes transfer. If anything fails, the sender reattempts from start (configurable `--retries`).
2. **Chunked/resumable**: file is split into fixed-size chunks (e.g., 4MiB). Each chunk has a chunk-level checksum. Receiver can report which chunks are present, allowing sender to transmit only missing chunks.

**Chunked protocol notes**:

- `TransferStart` includes `chunked=true` and `chunk_size`.
- Sender transmits chunks: for each chunk send a small `ChunkHeader` protobuf, then raw chunk bytes, and optionally wait ack.
- Receiver records received chunks to a small `.ncp.meta` (or uses sparse temp file + bitset) so resume is possible after reconnect.

For the MVP, implement **simple mode** and add chunked/resume later.

---

## Security & TLS

**MVP Implementation**: Start with insecure mode (plain TCP) for simplicity and local testing. TLS support will be added in a later version.

- **MVP**: Plain TCP connections with `--insecure` flag (default for MVP).
- **Future**: TLS support using `tokio-rustls` or `rustls` for production use.
- **Future**: Support both server-only TLS and mutual TLS (mTLS) for stronger authentication.
- **Future**: PSK/HMAC for environments where TLS certificates are not feasible.

**Future Authentication ideas**:

- `--allowed-clients` list of client IDs (UUID) or fingerprints.
- Use client certificates (mTLS) and validate against a CA or fingerprint.

---





# Exit codes & error model

## Exit codes (process-level)

These are the canonical exit codes used by both `ncp send` and `ncp recv` so scripts and CI can react reliably.

* `0` — **SUCCESS**: File transferred and checksum validated.
* `1` — **GENERAL\_ERROR**: Unclassified error; check logs for details.
* `2` — **PROTOCOL\_ERROR**: Protocol violation or message framing error.
* `3` — **IO\_ERROR**: Local I/O failure (read/write) unrelated to permission/space.
* `4` — **PERMISSION\_DENIED**: Receiver cannot write the file with required permissions.
* `5` — **CHECKSUM\_MISMATCH**: Transfer finished but checksum validation failed.
* `6` — **NO\_SPACE**: Receiver disk space insufficient.
* `7` — **TIMEOUT**: A timeout (connect, read, or write) occurred and retry policy exhausted.
* `8` — **AUTH\_FAILURE**: TLS/mTLS/PSK validation failed or identity mismatch.
* `9` — **USER\_ABORT**: User requested abort (SIGINT / interactive cancel).
* `10` — **INVALID\_ARGUMENT**: CLI or configuration error.
* `11` — **MAX\_RETRIES\_EXCEEDED**: Retries exhausted for recoverable errors.
* `12` — **RESUME\_NOT\_SUPPORTED**: Resume requested but not supported by peer.
* `13` — **UNEXPECTED\_EOF**: Underflow or stream truncation (EOF too early).
* `14` — **UNEXPECTED\_DATA**: Extra bytes after expected file size.

> Implementation note: combine process exit codes with machine-readable `TRANSFER_RESULT` control frames so orchestrators can interpret both program exit code and protocol-level result.

## Protocol error model

Errors are split into two categories: **fatal** (immediate abort, no retry) and **transient** (retryable). The protocol communicates errors via `Error` messages (Protobuf `Error`) and specialized frames (e.g., `PREFLIGHT_FAIL`, `TRANSFER_RESULT`).

### Fatal errors (no automatic retries)

* Permission denied (file ownership/mode).
* No space and policy says do not reclaim/overwrite.
* Capability mismatch for a required feature (e.g., TLS required by policy).
* Protocol version incompatibility.

### Transient errors (retryable)

* Network interruptions (connection reset, timeouts).
* Checksum mismatch (if resume supported, retry/resume may be attempted).
* Receiver-side transient filesystem errors (EIO), if retry policy permits.

### Retry policy (configurable)

* `--retries N` (default 3)
* Backoff: exponential with jitter (e.g., base 2s → 2s, 4s, 8s + jitter)
* On checksum mismatch: if resume supported, attempt resume; otherwise restart from beginning.
* On transient network errors: reconnect and re-initiate protocol (respect session IDs).

---

# Wire format & framing

## High-level rules

* **Transport**: Plain TCP (MVP), TLS support in future versions.
* **Single-stream default**: Control messages (Protobuf) + raw-bytes data transfer on the same TCP connection.
* **Framing**: All Protobuf control messages are length-prefixed with a 4-byte big-endian unsigned integer (u32 BE) representing the byte length of the following Protobuf-encoded message.

### Frame format (control frames)

```
+----------------+--------------------+
| 4 bytes (BE)   | protobuf bytes     |
+----------------+--------------------+
```

* The 4-byte prefix is the length `L` of the protobuf message that follows (`0 < L <= 2^32-1`).
* Multiple control frames are sent in sequence as needed.

### Switching to raw data

* After `TRANSFER_START` control frame (a Protobuf message indicating `transfer_mode = RAW_BYTES` and `file_size`), the **sender** writes exactly `file_size` raw bytes *immediately after* that control frame with **no additional framing**.
* The receiver reads exactly `file_size` bytes; if the sender closes or EOF occurs before `file_size` bytes are received, the receiver treats it as `UNEXPECTED_EOF`.
* After consuming `file_size` bytes, the **receiver** sends a `TRANSFER_RESULT` control frame describing success/failure and checksum.

> Rationale: This approach avoids double-encoding payloads (no base64) and maximizes throughput. It keeps control messages reliably framed with fixed-length prefix.

---

# Protobuf schema (proto3)

Save as `proto/ncp.proto`. This schema covers control messages needed for the single-stream protocol, capabilities, preflight, resume negotiation, and errors.

> Note: checksums in messages are raw bytes (binary). Convert to/from hex in logging or CLI output.

---

# State machine (sequence & diagrams)

## Sequence (single-file, no-resume)

1. Sender → connect TCP → send `Probe`.
2. Receiver → respond `Established`.
3. Sender → send `Meta`.
4. Receiver → perform preflight checks (space, permissions) → send `PreflightOk` or `PreflightFail`.

   * On `PreflightFail` sender exits with failure.
5. Sender → send `TransferStart` (`mode = TRANSFER_RAW`, `file_size`).
6. Sender → stream exactly `file_size` raw bytes.
7. Receiver → write to temp file, fsync if configured, compute checksum.
8. Receiver → send `TransferResult` with `ok=true` and checksum or `ok=false` with `ErrorCode`.
9. Sender → interpret result; on success exit `0`, on failure follow retry/resume policy.

## Sequence (resume)

* After a failed transfer, sender requests resume by sending `Probe` with `capabilities` including resume.
* Receiver responds and/or `OffsetReport` describes how many bytes saved.
* Sender sends `ResumeRequest` with `offset`.
* Transfer proceeds from given offset (sender must seek file and stream remaining bytes).

---

# CLI reference

## Basic usage examples

```bash
# Phase 1 - Minimal examples (insecure mode only)

# send a file to remote receiver
ncp send --host remote.example.com --port 9000 /path/to/foo.bin

# run receiver and store files in ./out
ncp recv --port 9000 ./out

# send directory
ncp send --host receiver.example.com --port 9000 ./my_folder

# send file with retries and basic checksum
ncp send --host receiver.example.com --port 9000 --retries 5 --checksum hash ./bigdata.bin

# receive and overwrite existing files automatically
ncp recv --port 9000 --overwrite yes /data/incoming

# Phase 2+ examples (future)
# ncp send --host 10.0.0.5 --port 9000 --checksum sha256 --verbose big.iso
# ncp recv --port 9000 --max-size 1GB --timeout 60s /data/incoming
```

## CLI flags (grouped)

### Common (Phase 1 - Minimal)

* `--retries N` (default `3`)

### Send (Phase 1 - Minimal)

* `send --host HOST --port PORT SRC`
* `--checksum [hash|none]` (default hash - uses std library hasher)
* `SRC` - source file or directory (required positional argument)

### Receive (Phase 1 - Minimal)

* `recv --port PORT DST`
* `--overwrite [ask|yes|no]` (default ask - prompt user for conflicts)
* `DST` - destination file or directory (required positional argument)

### File/Directory Handling (Phase 1)

* Auto-detects if `SRC`/`DST` is file or directory
* `SRC` directory → `DST` directory: creates same structure
* `SRC` file → `DST` directory: creates file inside directory  
* `SRC` file → `DST` file: overwrites destination file
* **Forbidden**: `SRC` directory → `DST` file

### Future Additions (Phase 2+)

* `--bind HOST` (receiver bind address)
* `--verbose`, `-v`: increase logging
* `--config FILE`: path to YAML/JSON config file
* `--timeout DURATION` (e.g., `30s`)
* `--checksum sha256|xxhash64|none` (better algorithms)
* `--chunk-size SIZE` (chunked mode)
* `--resume` (attempt resume)
* `--max-size BYTES` (reject above threshold)
* `--tls` (enable TLS)
* `--allowlist HOSTS` (IP/CIDR filtering)
* `--temp-dir PATH` (custom temp directory)
* `--rate-limit KB/s` (bandwidth limiting)

---

# Implementation notes (Rust)

## Recommended crates

**Minimal Phase 1 crates** (smallest binary):
* **Protobuf**: `prost` (prost-build in build.rs) - essential for protocol
* **CLI**: `clap` (v4) with minimal features - essential for usability
* **Checksum**: `std::collections::hash_map::DefaultHasher` or `std::hash` - no external deps
* **Error handling**: `std::error::Error` - no external deps
* **Networking**: `std::net::TcpStream` - no async runtime initially
* **File I/O**: `std::fs` and `std::io` - no external deps

**Phase 2 additions** (after minimal version works):
* **Async runtime**: `tokio` for better performance
* **Better checksums**: `sha2` crate for sha256
* **Logging**: `tracing` & `tracing-subscriber`
* **Error handling**: `thiserror` + `anyhow`

**Future additions**:
* **TLS**: `tokio-rustls` for secure connections
* **Additional checksums**: `twox-hash` for xxhash

## Project layout (Phase 1 - Minimal)

```
ncp/
├─ Cargo.toml          # minimal dependencies
├─ build.rs            # build prost artifacts
├─ proto/ncp.proto     # protocol definition
├─ src/
│  ├─ main.rs          # CLI and main logic
│  ├─ send.rs          # sender implementation
│  ├─ recv.rs          # receiver implementation
│  ├─ framing.rs       # protobuf framing utilities
│  ├─ proto.rs         # prost generated types
│  └─ checksum.rs      # std library checksum
```

**Phase 2+ additions**:
```
├─ src/
│  ├─ tls.rs           # TLS support
│  ├─ util.rs          # utilities
│  ├─ errors.rs        # error types
│  └─ config.rs        # configuration
```

## Protobuf build

Use `prost-build` in `build.rs`:

```rust
// build.rs (sketch)
fn main() -> Result<(), Box<dyn std::error::Error>> {
    prost_build::Config::new()
        .out_dir("src/proto") // or let prost-build generate mod.rs
        .compile_protos(&["proto/ncp.proto"], &["proto"])?;
    Ok(())
}
```

## Framing helpers

* `read_frame(socket) -> bytes`: read 4-byte BE length, then exactly that many bytes.
* `write_frame(socket, prost_msg)`: encode prost message into bytes, prefix with 4-byte BE length, write.

## Raw-data transfer (Phase 1 - Synchronous)

* After writing `TransferStart` frame, use `std::io::copy` from `File` to `TcpStream`.
* To compute checksum while streaming: read chunks from file, update `DefaultHasher`, and write to socket. Use 64KiB buffers.
* No async I/O initially - keeps binary small and dependencies minimal.

## Atomic write and fsync (Phase 1 - Synchronous)

* Receiver writes to `temp_path = dest + ".ncp." + session_id`.
* After full write, call `file.sync_all()?` if fsync enabled.
* Rename with `std::fs::rename(temp_path, final_path)?`.
* Set permissions using `std::fs::set_permissions` with `std::fs::Permissions`.

**Phase 2+ improvements**:
* Switch to async I/O with tokio for better performance
* Better error handling and timeout support

## Handling large files

* Use `u64` for sizes.
* Avoid loading file into memory; stream in small buffers.
* Provide configurable `buffer_size`.

## Signal handling

* Intercept `SIGINT`/`SIGTERM` for graceful abort: stop reads/writes, send `Error` frame if possible, cleanup temp files if configured.

---

# Tests & validation checklist

## Unit tests

* Framing: partial reads, large frames, truncated frames
* Protobuf encode/decode roundtrips
* Checksum functions on known inputs
* Temp file rename, permission setting (platform-specific)

## Integration tests

* Send small file (1 KB) local loopback
* Send large file (4 GiB) test (use sparse file)
* Simulate network cut (drop connection mid-transfer) and verify `UNEXPECTED_EOF`
* Resume test: interrupt mid-transfer; start sender with `--resume`; verify file completes and checksum matches
* Permission error test: receiver directory non-writable → `PREFLIGHT_FAIL`
* Disk full test (simulate by creating loop device or set `--max-size`) → `PREFLIGHT_FAIL`
* TLS test: mutual TLS verification, cert pinning test
* Cross-platform: Linux ↔ Windows tests for path and permission semantics

## Fuzz tests

* Corrupt frames (change length) and ensure receiver exits gracefully with `PROTOCOL_ERROR`.
* Large number of sequential small transfers to check resource leaks.

---

# Operational notes

## Security

**MVP Security Notes**:
* **WARNING**: MVP uses plain TCP (`--insecure` mode). Only use on trusted networks.
* Never run receiver with `--bind 0.0.0.0` in untrusted environments in MVP.
* MVP is suitable for local testing and trusted LAN environments only.

**Future Security Features**:
* TLS by default with `--insecure` flag to disable.
* PSK usage: use high-entropy keys and protect the key file (0600).
* Certificate management: support rotating server certs and a CA allowlist.

## NAT and firewalls

* Single-stream model requires the **sender** to connect to receiver (incoming on reply side). If you need reverse connect (receiver initiates), add a `reverse` or `listen` flag where sender binds and receiver connects.
* Avoid ephemeral additional TCP ports to simplify firewall traversal.

## Monitoring & metrics

* Expose a small HTTP metrics endpoint (Prometheus) optional (`--metrics-port`) with counters: `bytes_sent`, `bytes_received`, `transfers_total`, `transfers_failed`, `transfer_duration_seconds`.

## Logging

* Use structured logging (`tracing`), include `session_id` in logs.
* For automation: support JSON logs mode.

---

# Examples

### Example: Minimal send (Phase 1 - insecure only)

```bash
# receiver (on receiver.host)
ncp recv --port 9000 /data/inbox

# sender
ncp send --host receiver.host --port 9000 ./video.mp4
```

### Example: Future TLS + resume (not in MVP)

```bash
# Future: receiver with cert (not in MVP)
# ncp recv --port 9000 --tls-cert /etc/ncp/cert.pem --tls-key /etc/ncp/key.pem /data/inbox

# Future: sender with TLS and resume support (not in MVP)
# ncp send --host receiver.host --port 9000 --tls --resume --retries 5 ./big.iso
```

---

# Roadmap & development phases

## Phase 1 - Minimal Working Version
* Single-stream Protobuf control + raw-bytes transfer
* Basic std library checksum (DefaultHasher)
* Atomic rename with temp files
* Insecure mode only (plain TCP)
* Basic retries (3 attempts)
* Minimal CLI (host, port, file, dir)
* Synchronous I/O with std library
* **Goal**: Smallest possible binary, fewest dependencies

## Phase 2 - Enhanced Functionality
* Async I/O with tokio
* Better checksums (sha256)
* Logging and verbose output
* Timeout handling
* Configuration files
* Better error messages

## Phase 3 - Security & Reliability
* TLS support with tokio-rustls
* Resume & chunked transfers
* Rate limiting
* Connection allowlists

## Phase 4 - Advanced Features
* Multi-file transfers
* Compression support
* Metrics and monitoring
* Cross-platform optimizations

## Phase 5 - Production Ready
* GUI interface
* Daemon mode
* Package and release binaries

---

# Appendix: Quick design decisions & rationale (summary)

* **Single-stream** default to avoid NAT & firewall complications.
* **Protobuf** for control messages: strong typing, compact, fast parsing.
* **Raw-bytes transfer** for performance (no base64).
* **Atomic write** with temp file + rename + fsync to avoid partial files.
* **MVP: Insecure mode first** for simplicity and local testing; TLS added in future versions.
* **Rust** for memory safety and performance; use `tokio` for async I/O.

---
