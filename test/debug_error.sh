#!/bin/bash

echo "Debugging the socket error..."

# Create test file
echo "debug test" > debug.txt

# Start receiver with maximum verbosity
echo "Starting receiver with debug output..."
timeout 10s ./target/release/ncp -vv recv --listen --port 1234 debug_received.txt &
RECV_PID=$!

sleep 2

echo "Starting sender with debug output..."
./target/release/ncp -vv send --host 127.0.0.1 --port 1234 debug.txt

wait $RECV_PID

echo "Checking results..."
ls -la debug*

rm -f debug.txt debug_received.txt