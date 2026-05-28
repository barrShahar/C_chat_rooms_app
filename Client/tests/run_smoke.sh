#!/bin/bash
set -u

cd "$(dirname "$0")/.."
REPO_ROOT="$(cd .. && pwd)"

SERVER_BIN="$REPO_ROOT/build/out.serverMain"
CLIENT_BIN="$REPO_ROOT/build/out.client"

if [ ! -x "$SERVER_BIN" ]; then
    echo "[smoke] ERROR: missing $SERVER_BIN. Run 'make' at repo root first." >&2
    exit 2
fi
if [ ! -x "$CLIENT_BIN" ]; then
    echo "[smoke] ERROR: missing $CLIENT_BIN. Run 'make' at repo root first." >&2
    exit 2
fi

mkdir -p log
TIMESTAMP=$(date +%H%M%S)
SERVER_LOG="log/server_${TIMESTAMP}.log"
CLIENT_LOG="log/client_${TIMESTAMP}.log"

SERVER_PID=""
cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
}
trap cleanup EXIT INT TERM

"$SERVER_BIN" </dev/null &>"$SERVER_LOG" &
SERVER_PID=$!
echo "[smoke] Server PID=$SERVER_PID, log=$SERVER_LOG"

echo "[smoke] Waiting for server on port 8080..."
deadline=$(( $(date +%s) + 10 ))
while ! ss -tlnp 2>/dev/null | grep -q ':8080 '; do
    if [ "$(date +%s)" -ge "$deadline" ]; then
        echo "[smoke] ERROR: server did not open port 8080 within 10 seconds" >&2
        exit 1
    fi
    sleep 0.1
done
echo "[smoke] Server ready. Running client..."

# Scripted input:
#   1            -> Connect (DISCONNECTED menu)
#   1            -> Register (CONNECTED menu)
#   alice_<ts>   -> username
#   pass         -> password
#   7            -> Exit (LOGGED_IN menu; Register transitions to LOGGED_IN)
SUFFIX=$(date +%s)
USER="alice_${SUFFIX}"
printf '1\n1\n%s\npass\n7\n' "$USER" | "$CLIENT_BIN" &>"$CLIENT_LOG"
CLIENT_RC=$?

echo "[smoke] Client exit=$CLIENT_RC, log=$CLIENT_LOG"

if [ "$CLIENT_RC" -ne 0 ]; then
    echo "[smoke] FAIL: client exited non-zero" >&2
    exit 1
fi

# Server logs "Registering user: <name>" on receipt and "User added" on success.
if ! grep -q "Registering user: $USER" "$SERVER_LOG"; then
    echo "[smoke] FAIL: server did not log registration for $USER" >&2
    echo "----- server log tail -----" >&2
    tail -n 30 "$SERVER_LOG" >&2 || true
    exit 1
fi

echo "[smoke] PASS"
