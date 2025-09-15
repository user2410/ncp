#!/bin/bash

echo "Testing the actual issue..."

# The problem: In port forwarding, we need:
# Container: recv --listen (waits for connection, then receives files)
# Local: send --host (connects, then sends files)

echo "This should work (traditional):"
echo "recv listens, send connects"

echo "test data" > test.txt

echo "Starting recv in listen mode..."
timeout 5s ./target/release/ncp recv --listen --port 1234 --overwrite yes received.txt &
RECV_PID=$!
sleep 1

echo "Starting send in connect mode..."
./target/release/ncp send --host 127.0.0.1 --port 1234 test.txt
SEND_EXIT=$?

wait $RECV_PID
RECV_EXIT=$?

if [ $SEND_EXIT -eq 0 ] && [ -f received.txt ]; then
    echo "✅ This works! recv listens, send connects"
else
    echo "❌ Even this basic case fails (send_exit=$SEND_EXIT, recv_exit=$RECV_EXIT)"
fi

rm -f test.txt received.txt