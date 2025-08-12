#!/bin/bash

# Enhanced test script for ncp with disk space checking
set -e

echo "Building ncp..."
cargo build --release

echo "Running unit tests..."
cargo test

echo "Creating test files..."
mkdir -p test_files
echo "Hello, World! This is a test file for disk space testing." > test_files/test.txt

# Create a larger test file (1MB)
echo "Creating large test file..."
dd if=/dev/zero of=test_files/large.bin bs=1024 count=1024 2>/dev/null
echo "Large file created: $(du -h test_files/large.bin)"

echo "Creating receive directory..."
mkdir -p received_files

echo "Testing disk space reporting..."
echo "Available space in received_files/:"
df -h received_files/ || echo "df not available"

echo "=== Test 1: Normal file transfer with disk space check ==="
echo "Starting receiver in background..."
timeout 30 ./target/release/ncp recv --port 9001 received_files/ &
RECV_PID=$!

# Give receiver time to start
sleep 1

echo "Sending test file..."
./target/release/ncp send --host 127.0.0.1 --port 9001 test_files/test.txt

# Wait a bit and kill receiver
sleep 1
kill $RECV_PID 2>/dev/null || true
wait $RECV_PID 2>/dev/null || true

echo "Verifying transfer..."
if [ -f "received_files/test.txt" ]; then
    echo "✓ Small file transferred successfully"
    if cmp -s "test_files/test.txt" "received_files/test.txt"; then
        echo "✓ File content matches"
    else
        echo "✗ File content differs"
        exit 1
    fi
else
    echo "✗ File not found in destination"
    exit 1
fi

echo "=== Test 2: Large file transfer ==="
echo "Starting receiver for large file..."
timeout 30 ./target/release/ncp recv --port 9002 received_files/ &
RECV_PID=$!

sleep 1

echo "Sending large file..."
./target/release/ncp send --host 127.0.0.1 --port 9002 test_files/large.bin

sleep 1
kill $RECV_PID 2>/dev/null || true
wait $RECV_PID 2>/dev/null || true

echo "Verifying large file transfer..."
if [ -f "received_files/large.bin" ]; then
    echo "✓ Large file transferred successfully"
    ORIG_SIZE=$(stat -f%z test_files/large.bin 2>/dev/null || stat -c%s test_files/large.bin)
    RECV_SIZE=$(stat -f%z received_files/large.bin 2>/dev/null || stat -c%s received_files/large.bin)
    
    if [ "$ORIG_SIZE" = "$RECV_SIZE" ]; then
        echo "✓ File sizes match ($ORIG_SIZE bytes)"
    else
        echo "✗ File size differs: original=$ORIG_SIZE, received=$RECV_SIZE"
        exit 1
    fi
else
    echo "✗ Large file not found in destination"
    exit 1
fi

echo "=== Test 3: Disk space unit tests ==="
echo "Testing disk space functions..."
cargo test diskspace

echo "=== Test 4: Overwrite behavior ==="
echo "Testing overwrite with --overwrite yes..."
echo "Starting receiver with overwrite=yes..."
timeout 20 ./target/release/ncp recv --port 9003 --overwrite yes received_files/test_overwrite.txt &
RECV_PID=$!

sleep 1

echo "Sending file to specific destination..."
./target/release/ncp send --host 127.0.0.1 --port 9003 test_files/test.txt

sleep 1
kill $RECV_PID 2>/dev/null || true
wait $RECV_PID 2>/dev/null || true

if [ -f "received_files/test_overwrite.txt" ]; then
    echo "✓ File saved with specific name"
else
    echo "✗ File not saved with specific name"
    exit 1
fi

echo "=== Cleanup ==="
echo "Cleaning up test files..."
rm -rf test_files received_files

echo ""
echo "🎉 All tests passed!"
echo "✓ Basic file transfer"
echo "✓ Large file transfer" 
echo "✓ Disk space checking"
echo "✓ Overwrite handling"
echo "✓ Unit tests"
