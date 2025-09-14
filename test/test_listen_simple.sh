#!/bin/bash

echo "Testing listen mode syntax and basic functionality..."

# Test that listen mode starts correctly
echo "=== Test: Listen mode starts ==="
echo "test data" > listen_test.txt

echo "Starting sender in listen mode (should show 'Listening on port 1234')..."
timeout 3s ./target/release/ncp send --listen --port 1234 listen_test.txt &
LISTEN_PID=$!
sleep 1

# Check if it's listening
if netstat -an | grep -q ":1234.*LISTEN"; then
    echo "✅ Listen mode started successfully - port 1234 is listening"
else
    echo "❌ Listen mode failed - port 1234 not listening"
fi

kill $LISTEN_PID 2>/dev/null
wait 2>/dev/null

# Test help shows listen option
echo ""
echo "=== Test: Help shows listen option ==="
if ./target/release/ncp --help | grep -q "\-\-listen"; then
    echo "✅ Help shows --listen option"
else
    echo "❌ Help missing --listen option"
fi

# Test error handling
echo ""
echo "=== Test: Error handling ==="
if ./target/release/ncp send --port 1234 listen_test.txt 2>&1 | grep -q "host required"; then
    echo "✅ Correctly requires --host when not using --listen"
else
    echo "❌ Error handling for missing --host failed"
fi

echo ""
echo "=== Port Forwarding Usage ==="
echo "For ECS container with SSM port forwarding:"
echo ""
echo "1. On ECS container (remote):"
echo "   ncp send --listen --port 1234 /path/to/file.txt"
echo ""
echo "2. On local machine:"
echo "   aws ssm start-session --target i-xxxxx \\"
echo "     --document-name AWS-StartPortForwardingSession \\"
echo "     --parameters 'portNumber=[1234],localPortNumber=[3456]'"
echo ""
echo "3. On local machine (another terminal):"
echo "   ncp recv --host 127.0.0.1 --port 3456 ./received_file.txt"
echo ""
echo "This creates: ECS:1234 <-> SSM <-> Local:3456"

rm -f listen_test.txt