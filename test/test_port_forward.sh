#!/bin/bash

echo "Testing Port Forwarding Scenario..."

# Build first
cargo build --release

echo "=== Correct Port Forwarding Usage ==="
echo "test data for port forwarding" > pf_test.txt

echo "Step 1: Start sender in listen mode (simulates ECS container)"
timeout 10s ./target/release/ncp send --listen --port 1234 pf_test.txt &
SENDER_PID=$!
sleep 2

echo "Step 2: Start receiver in connect mode (connects to forwarded port)"
echo "Note: In real scenario, SSM would be forwarding 1234->3456"
echo "For test, we connect receiver directly to sender"
./target/release/ncp recv --host 127.0.0.1 --port 1234 --overwrite yes pf_received.txt
RECV_EXIT=$?

wait $SENDER_PID
SENDER_EXIT=$?

echo ""
if [ $RECV_EXIT -eq 0 ] && [ -f pf_received.txt ]; then
    if cmp pf_test.txt pf_received.txt; then
        echo "✅ Port forwarding scenario WORKS!"
        echo "✅ File transferred successfully"
        echo "✅ Content matches"
    else
        echo "❌ File content mismatch"
    fi
else
    echo "❌ Transfer failed (recv_exit=$RECV_EXIT, sender_exit=$SENDER_EXIT)"
fi

echo ""
echo "=== Real World Usage ==="
echo ""
echo "1. On ECS Container:"
echo "   ncp send --listen --port 1234 /path/to/file.txt"
echo ""
echo "2. On Local Machine (Terminal 1 - Start SSM Port Forward):"
echo "   aws ssm start-session --target i-xxxxx \\"
echo "     --document-name AWS-StartPortForwardingSession \\"
echo "     --parameters 'portNumber=[1234],localPortNumber=[3456]'"
echo ""
echo "3. On Local Machine (Terminal 2 - Connect to forwarded port):"
echo "   ncp recv --host 127.0.0.1 --port 3456 ./received_file.txt"
echo ""
echo "Key difference: recv uses --host (connect mode) instead of just --port (listen mode)"
echo "This prevents 'address already in use' error since SSM owns the local port."

# Cleanup
rm -f pf_test.txt pf_received.txt