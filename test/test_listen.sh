#!/bin/bash

echo "Testing listen mode for port forwarding scenarios..."

# Build first
cargo build --release

# Test 1: Send in listen mode, recv connects to it
echo "=== Test 1: Send listen mode ==="
echo "test data for listen" > test_listen.txt

echo "Starting sender in listen mode on port 1234..."
timeout 10s ./target/release/ncp send --listen --port 1234 test_listen.txt &
SEND_PID=$!
sleep 2

echo "Connecting receiver to sender..."
./target/release/ncp recv --host 127.0.0.1 --port 1234 --overwrite yes received_from_listen.txt
RECV_EXIT=$?

wait $SEND_PID
SEND_EXIT=$?

if [ $RECV_EXIT -eq 0 ] && [ -f received_from_listen.txt ]; then
    if cmp test_listen.txt received_from_listen.txt; then
        echo "✅ Test 1 PASSED: Listen mode works"
    else
        echo "❌ Test 1 FAILED: File content mismatch"
    fi
else
    echo "❌ Test 1 FAILED: Transfer failed (recv_exit=$RECV_EXIT, send_exit=$SEND_EXIT)"
fi

# Test 2: Traditional mode (for comparison)
echo ""
echo "=== Test 2: Traditional mode ==="
echo "traditional test data" > test_traditional.txt

echo "Starting receiver on port 1235..."
timeout 10s ./target/release/ncp recv --port 1235 --overwrite yes received_traditional.txt &
RECV_PID=$!
sleep 2

echo "Connecting sender to receiver..."
./target/release/ncp send --host 127.0.0.1 --port 1235 test_traditional.txt
SEND_EXIT=$?

wait $RECV_PID
RECV_EXIT=$?

if [ $SEND_EXIT -eq 0 ] && [ -f received_traditional.txt ]; then
    if cmp test_traditional.txt received_traditional.txt; then
        echo "✅ Test 2 PASSED: Traditional mode works"
    else
        echo "❌ Test 2 FAILED: File content mismatch"
    fi
else
    echo "❌ Test 2 FAILED: Transfer failed (send_exit=$SEND_EXIT, recv_exit=$RECV_EXIT)"
fi

# Test 3: Port forwarding simulation
echo ""
echo "=== Test 3: Port forwarding simulation ==="
echo "port forward simulation data" > test_pf.txt

echo "Step 1: Start sender in listen mode (simulates remote ECS container)"
timeout 15s ./target/release/ncp send --listen --port 1234 test_pf.txt &
REMOTE_SENDER_PID=$!
sleep 1

echo "Step 2: Start receiver in listen mode (simulates local forwarded port)"
timeout 15s ./target/release/ncp recv --port 3456 --overwrite yes pf_received.txt &
LOCAL_RECV_PID=$!
sleep 1

echo "Step 3: Connect local sender to local receiver (simulates port forwarding)"
./target/release/ncp send --host 127.0.0.1 --port 3456 test_pf.txt &
LOCAL_SEND_PID=$!
sleep 1

echo "Step 4: Connect local receiver to remote sender (simulates forwarded connection)"
./target/release/ncp recv --host 127.0.0.1 --port 1234 --overwrite yes final_pf_received.txt
FINAL_EXIT=$?

# Cleanup
kill $REMOTE_SENDER_PID $LOCAL_RECV_PID $LOCAL_SEND_PID 2>/dev/null
wait 2>/dev/null

if [ $FINAL_EXIT -eq 0 ] && [ -f final_pf_received.txt ]; then
    if cmp test_pf.txt final_pf_received.txt; then
        echo "✅ Test 3 PASSED: Port forwarding simulation works"
    else
        echo "❌ Test 3 FAILED: File content mismatch"
    fi
else
    echo "❌ Test 3 FAILED: Port forwarding simulation failed (exit=$FINAL_EXIT)"
fi

echo ""
echo "=== Summary ==="
echo "Listen mode allows ncp to work like nc:"
echo "- Remote: ncp send --listen --port 1234 file.txt"
echo "- Local:  ncp recv --host 127.0.0.1 --port 3456 output.txt"
echo "- SSM:    aws ssm start-session --target i-xxx --document-name AWS-StartPortForwardingSession --parameters 'portNumber=[1234],localPortNumber=[3456]'"

# Cleanup
rm -f test_listen.txt received_from_listen.txt test_traditional.txt received_traditional.txt test_pf.txt pf_received.txt final_pf_received.txt