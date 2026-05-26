#!/bin/bash
cd "$(dirname "$0")"

cleanup() {
    kill "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
}
trap cleanup EXIT INT TERM

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
SERVER_LOG="build/server_${TIMESTAMP}.log"
CLIENT_LOG="build/client_${TIMESTAMP}.log"

# /dev/tty connects server stdin to the real terminal so Enter still works
./build/out.test_app < /dev/tty &>"$SERVER_LOG" &
SERVER_PID=$!
echo "[run_test] Server started (PID=$SERVER_PID), logs -> $SERVER_LOG"

sleep 0.3
echo "[run_test] Launching multi-client, logs -> $CLIENT_LOG"

./build/out.multi_client &>"$CLIENT_LOG"

echo "[run_test] Multi-client done, shutting down server..."
