# NCP Implementation Guide

This is a step-by-step implementation of the minimal NCP file transfer tool as specified in README-MINIMAL.md.

## Project Structure

```
ncp/
├── Cargo.toml              # Rust dependencies and build config
├── build.rs                # Protobuf compilation script
├── proto/
│   └── ncp.proto          # Protocol buffer definitions
├── src/
│   ├── main.rs            # CLI entry point and argument parsing
│   ├── proto.rs           # Generated protobuf code wrapper
│   ├── framing.rs         # Message length framing utilities
│   ├── checksum.rs        # Checksum calculation utilities
│   ├── diskspace.rs       # Disk space checking utilities
│   ├── recv.rs            # Receiver implementation
│   └── send.rs            # Sender implementation
├── test.sh                # Basic integration test
└── IMPLEMENTATION.md      # This file
```

## Build Instructions

1. **Install Rust** (if not already installed):
   ```bash
   curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
   source ~/.cargo/env
   ```

2. **Create the project directory**:
   ```bash
   mkdir ncp
   cd ncp
   ```

3. **Create the proto directory**:
   ```bash
   mkdir proto
   ```

4. **Copy the protobuf file** (`ncp.proto`) into `proto/ncp.proto`

5. **Create all the Rust source files** as shown in the artifacts above

6. **Build the project**:
   ```bash
   cargo build --release
   ```

The binary will be created at `target/release/ncp`.

## Usage Examples

### Basic File Transfer

**Terminal 1 (Receiver)**:
```bash
./target/release/ncp recv --port 9000 ./downloads/
```

**Terminal 2 (Sender)**:
```bash
./target/release/ncp send --host 127.0.0.1 --port 9000 ./myfile.txt
```

### With Options

**Receiver with specific output file**:
```bash
./target/release/ncp recv --port 9000 --overwrite yes ./output.txt
```

**Sender with retries and no checksum**:
```bash
./target/release/ncp send --host 192.168.1.100 --port 9000 --retries 5 --checksum none ./data.bin
```

## Testing

Run the basic integration test:
```bash
chmod +x test.sh
./test.sh
```

Or test manually:

1. **Start receiver**:
   ```bash
   ./target/release/ncp recv --port 9000 ./received/
   ```

2. **Send a file** (in another terminal):
   ```bash
   echo "Hello NCP!" > test.txt
   ./target/release/ncp send --host 127.0.0.1 --port 9000 test.txt
   ```

3. **Verify**:
   ```bash
   cat received/test.txt
   ```

## Implementation Notes

### Key Features Implemented

1. **TCP Connection Management**: Uses `std::net::TcpStream` and `TcpListener`
2. **Protobuf Message Framing**: 4-byte big-endian length prefix for all control messages
3. **File Streaming**: Raw bytes after `TransferStart` message
4. **Checksum Validation**: Using `std::collections::hash_map::DefaultHasher`
5. **Atomic File Writing**: Temporary files with atomic rename
6. **Overwrite Handling**: Ask/Yes/No modes for existing files
7. **Disk Space Checking**: Cross-platform available space validation with safety buffer
8. **Progress Display**: Simple byte counter during transfer
9. **Retry Logic**: Configurable retry attempts for failed transfers

### Protocol Flow

1. **Connection Setup**:
   - Sender connects and sends `Probe`
   - Receiver responds with `Established`

2. **Metadata Exchange**:
   - Sender sends `Meta` with file info and checksum
   - Receiver validates and sends `PreflightOk` or `PreflightFail`

3. **Data Transfer**:
   - Sender sends `TransferStart`
   - Sender streams raw file bytes (no framing)
   - Receiver writes to temporary file

4. **Completion**:
   - Receiver validates checksum and sends `TransferResult`
   - If successful, receiver atomically renames temp file to final name

### Error Handling

- Network errors trigger retries
- Checksum mismatches fail the transfer
- File permission issues are reported via `PreflightFail`
- All errors include descriptive messages

### Limitations (As Per Minimal Spec)

- No TLS/encryption (plain TCP only)
- No directory transfer (single files only)
- No resume capability
- No async I/O (synchronous only)
- Basic checksum algorithm (DefaultHasher)
- No rate limiting or advanced features

## Next Steps

To extend this implementation:

1. **Add directory support**: Recursive file enumeration and transfer
2. **Add TLS**: Use `rustls` or `native-tls` for encryption
3. **Add async I/O**: Convert to `tokio` for better concurrency
4. **Add better checksums**: SHA-256, BLAKE2, etc.
5. **Add resume support**: Partial transfer recovery
6. **Add progress bars**: Better UX with `indicatif`
7. **Add logging**: Structured logging with `tracing`

The current implementation provides a solid foundation that follows the protobuf protocol exactly as specified.
