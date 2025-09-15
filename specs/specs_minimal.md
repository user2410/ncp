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

### Traditional Usage
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

### Port Forwarding Usage (ECS + SSM)

**✅ Scenario 1: Container → Local (Container sends file)**
```bash
# On ECS Container (sender listens)
ncp send --listen --port 1234 /app/data/file.txt

# On Local Machine (receiver connects through port forward)
# Terminal 1: aws ssm start-session --target i-xxx --document-name AWS-StartPortForwardingSession --parameters 'portNumber=[1234],localPortNumber=[3456]'
# Terminal 2:
ncp recv --host 127.0.0.1 --port 3456 ./received_files/
```

**✅ Scenario 2: Local → Container (Container receives file)**
```bash
# On ECS Container (receiver listens)
ncp recv --listen --port 1234 /tmp/received_files/

# On Local Machine (sender connects through port forward)
# Terminal 1: aws ssm start-session --target i-xxx --document-name AWS-StartPortForwardingSession --parameters 'portNumber=[1234],localPortNumber=[3456]'
# Terminal 2:
ncp send --host 127.0.0.1 --port 3456 ./local_file.txt
```

## CLI Syntax

```
# Receiver (Listen Mode)
ncp recv [options] --port {port} {dst}
ncp recv [options] --listen --port {port} {dst}

# Receiver (Connect Mode - for port forwarding)
ncp recv [options] --host {host} --port {port} {dst}

# Sender (Connect Mode)
ncp send [options] --host {host} --port {port} {src}

# Sender (Listen Mode - for port forwarding)
ncp send [options] --listen --port {port} {src}
```

## CLI Options

### Common
- `-v, --verbose` (increase verbosity: -v info, -vv debug, -vvv trace)
- `--retries N` (default: 3)
- `--overwrite [ask|yes|no]` (default: ask)

### Send
- `--host HOST` (required for connect mode)
- `--port PORT` (required)
- `--listen, -l` (listen mode for port forwarding)
- `src` - source file or directory (required)

### Receive
- `--host HOST` (enables connect mode for port forwarding)
- `--port PORT` (required)
- `--listen, -l` (explicit listen mode, useful for containers)
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

## Port Forwarding Support

**Problem**: SSM port forwarding binds the local port, causing "address already in use" errors.

**Solution**: Use connect mode instead of listen mode on the local machine.

| Mode | Command | Behavior |
|------|---------|----------|
| **Listen** | `ncp recv --port 3456 file.txt` | ❌ Tries to bind port (fails with SSM) |
| **Connect** | `ncp recv --host 127.0.0.1 --port 3456 file.txt` | ✅ Connects to forwarded port |

**Mode Selection Logic:**
- `--host` specified → Connect mode
- `--listen` specified → Listen mode  
- Default (neither) → Listen mode

**Connection Flows**:

*Scenario 1 (Container sends):*
```
ECS Container:1234 <---> SSM Port Forward <---> Local:3456
     (sender)                                    (receiver)
   [listen mode]                              [connect mode]
```

*Scenario 2 (Container receives):*
```
Local:3456 <---> SSM Port Forward <---> ECS Container:1234
  (sender)                                    (receiver)
[connect mode]                              [listen mode]
```

**Key Points:**
1. **Container always uses `--listen`** (avoids SSM port binding conflicts)
2. **Local always uses `--host 127.0.0.1 --port {forwarded_port}`** (connects to SSM tunnel)
3. **Use directory paths** for destinations when possible (e.g., `./received_files/` not `./file.txt`)

**✅ Tested & Working:** Both scenarios have been tested and work correctly with the current implementation.

## Dependencies (Minimal)

* **None** - Uses only Rust standard library
* **Binary Size**: 378K (48% smaller than protobuf+clap version)
* **Protocol**: Simple binary format instead of protobuf
* **CLI**: Manual argument parsing instead of clap

## Project Structure

```
ncp/
├─ Cargo.toml
├─ src/
│  ├─ main.rs
│  ├─ send.rs
│  ├─ recv.rs
│  ├─ protocol.rs
│  ├─ directory.rs
│  ├─ diskspace.rs
│  └─ types.rs
├─ test/
│  └─ integration.sh
└─ USAGE.md
```

## Wire Format

- Control messages: `[type:u8][len:u32][data]` (big-endian)
- Raw data: exact file_size bytes with no framing after `TransferStart`
- No checksum validation (removed for minimal size)

## Key Messages (Binary Protocol)

- `MSG_META` (1) - file metadata (name, size, is_dir)
- `MSG_PREFLIGHT_OK` (2) - receiver validation success
- `MSG_PREFLIGHT_FAIL` (3) - receiver validation failure
- `MSG_TRANSFER_START` (4) - begin raw data transfer
- `MSG_TRANSFER_RESULT` (5) - final success/failure

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
- Checksum validation
- Protobuf dependencies
- Clap CLI parsing
- Bind address option (binds to all interfaces)
