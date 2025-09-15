#!/bin/bash

echo "Testing Reverse Port Forwarding Scenario..."
echo "Container as receiver, Local as sender"

# Build first
cargo build --release

echo "=== Reverse Port Forwarding Usage ==="
echo "reverse test data" > reverse_test.txt

echo "Step 1: Start receiver in listen mode (simulates ECS container)"
timeout 10s ./target/release/ncp recv --listen --port 1234 --overwrite yes reverse_received.txt &
RECV_PID=$!
sleep 2

echo "Step 2: Connect sender to receiver (simulates local machine connecting through port forward)"
./target/release/ncp send --host 127.0.0.1 --port 1234 reverse_test.txt
SEND_EXIT=$?

wait $RECV_PID
RECV_EXIT=$?

echo ""
if [ $SEND_EXIT -eq 0 ] && [ -f reverse_received.txt ]; then
    if cmp reverse_test.txt reverse_received.txt; then
        echo "✅ Reverse port forwarding scenario WORKS!"
        echo "✅ File transferred successfully"
        echo "✅ Content matches"
    else
        echo "❌ File content mismatch"
    fi
else
    echo "❌ Transfer failed (send_exit=$SEND_EXIT, recv_exit=$RECV_EXIT)"
fi

echo ""
echo "=== Real World Reverse Usage ==="
echo ""
echo "1. On ECS Container (receiver in listen mode):"
echo "   ncp recv --listen --port 1234 /tmp/received_file.txt"
echo ""
echo "2. On Local Machine (Terminal 1 - Start SSM Port Forward):"
echo "   aws ssm start-session --target i-xxxxx \\"
echo "     --document-name AWS-StartPortForwardingSession \\"
echo "     --parameters 'portNumber=[1234],localPortNumber=[3456]'"
echo ""
echo "3. On Local Machine (Terminal 2 - Send file):"
echo "   ncp send --host 127.0.0.1 --port 3456 ./local_file.txt"
echo ""
echo "Key: Container uses --listen to avoid binding conflicts with SSM"

# Cleanup
rm -f reverse_test.txt reverse_received.txt