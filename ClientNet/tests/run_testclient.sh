#!/bin/bash
cd "$(dirname "$0")/.."

cleanup() {
    kill "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
}
trap cleanup EXIT INT TERM

TIMESTAMP=$(date +%H%M)
SERVER_LOG="log/server_${TIMESTAMP}.log"
CLIENT_LOG="log/client_${TIMESTAMP}.log"

mkdir -p log

/home/barrs/Grunitech-Embedded/ChatProject/build/out.serverMain < /dev/tty &>"$SERVER_LOG" &
SERVER_PID=$!
echo "[run_testclient] Server started (PID=$SERVER_PID), logs -> $SERVER_LOG"

echo "[run_testclient] Waiting for server to be ready..."
deadline=$(( $(date +%s) + 10 ))
while ! ss -tlnp 2>/dev/null | grep -q ':8080 '; do
    if [ "$(date +%s)" -ge "$deadline" ]; then
        echo "[run_testclient] ERROR: server did not open port 8080 within 10 seconds" >&2
        exit 1
    fi
    sleep 0.1
done
echo "[run_testclient] Server ready. Launching test client, logs -> $CLIENT_LOG"

build/out.test_client_net &>"$CLIENT_LOG"

echo "[run_testclient] Client done, shutting down server..."
