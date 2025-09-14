# ncp — Minimal Version (Phase 1)

Lightweight file transfer over TCP with protobuf control messages. Minimal dependencies, smallest possible binary.

## Goals

- **Smallest binary**: absolute minimum dependencies, std library only where possible
- **Safe**: Rust memory safety and atomic file handling
- **Simple**: plain TCP, synchronous I/O, essential features only

## Protocol Overview

- Single TCP connection for control messages (protobuf) and raw file bytes
- Sender sends file metadata (`Meta`), receiver validates and replies (`PreflightResult`)
- If OK, sender streams raw bytes, receiver writes to file 

## CLI Usage

```bash
# Receive file to directory
ncp recv --port 9000 ./incoming

# Or receive file to specific file
ncp recv --port 9000 ./incoming/data.bin

# Send file
ncp send --host 127.0.0.1 --port 9000 ./data.bin

# Send directory
ncp send --host 127.0.0.1 --port 9000 ./my_folder

# With verbose output (debug level)
ncp send -vv --host 127.0.0.1 --port 9000 ./data.bin
```

## CLI Syntax

```
# Receiver
ncp recv [options] --port {port} {dst}

# Sender  
ncp send [options] --host {host} --port {port} {src}
```

## CLI Options

### Common
- `-v, --verbose` (increase verbosity: -v info, -vv debug, -vvv trace)
- `--retries N` (default: 3)
- `--overwrite [ask|yes|no]` (default: ask)

### Send
- `--host HOST` (required)
- `--port PORT` (required)
- `src` - source file or directory (required)

### Receive
- `--port PORT` (required)
- `dst` - destination file or directory (required)

## File/Directory Handling

- Auto-detects if `src`/`dst` is file or directory
- `src` directory → `dst` directory: creates same structure
- `src` file → `dst` directory: creates file inside directory
- `src` file → `dst` file: overwrites destination file
- **Forbidden**: `src` directory → `dst` file

## Overwrite Behavior

- `--overwrite ask` (default): prompt user for each conflict
- `--overwrite yes`: automatically overwrite existing files
- `--overwrite no`: skip existing files, continue transfer

## Dependencies (Minimal)

* **Protobuf**: `prost` (prost-build in build.rs) - essential for protocol
* **CLI**: `clap` with minimal features - essential for usability

## Project Structure

```
ncp/
├─ Cargo.toml
├─ build.rs
├─ proto/ncp.proto
├─ src/
│  ├─ main.rs
│  ├─ send.rs
│  ├─ recv.rs
│  ├─ framing.rs
│  ├─ proto.rs
```

## Wire Format

- Control messages: 4-byte big-endian length + protobuf bytes
- Raw data: exact file_size bytes with no framing after `TransferStart`

## Key Messages (Protobuf)

- `Meta` - file metadata (name, size)
- `PreflightResult` - receiver validation result
- `TransferStart` - begin raw data transfer
- `TransferResult` - final success/failure

## Implementation Notes

- **Networking**: `std::net::TcpStream` (synchronous)
- **File I/O**: `std::fs` and `std::io`
- **Error handling**: `std::error::Error`
- **Atomic write**: temp file + `std::fs::rename`

## Exit Codes

- `0` - Success
- `1` - General error
- `2` - Protocol error
- `3` - I/O error
- `4` - Permission denied
- `6` - No space
- `11` - Max retries exceeded

## Security Warning

**This minimal version uses plain TCP with no encryption. Only use on trusted networks.**

## What's NOT in Minimal Version

- TLS/encryption
- Async I/O
- Resume/chunking
- Configuration files
- Timeouts
- Rate limiting
- Bind address option (binds to all interfaces)
