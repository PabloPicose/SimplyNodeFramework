#!/usr/bin/env bash
# Runs the Autobahn|Testsuite (docker) against SNFWebSocket.
#   ./run_autobahn.sh <build-dir> [server|client|all]
# Build first with -DSNF_ENABLE_WEBSOCKET_CONFORMANCE=ON.
# HTML reports end up in reports/{server,client}/index.html.
# Exits non-zero if any case FAILED (see summarize.py).
set -euo pipefail

BUILD_DIR=$(realpath "${1:?usage: $0 <build-dir> [server|client|all]}")
MODE=${2:-all}
HERE=$(cd "$(dirname "$0")" && pwd)
BIN="$BUILD_DIR/lib/WebSocket/conformance"
IMAGE=crossbario/autobahn-testsuite:latest
PORT=9001
mkdir -p "$HERE/reports"

docker_args=(--rm --network host -u "$(id -u):$(id -g)"
             -v "$HERE/config:/config:ro" -v "$HERE/reports:/reports")

wait_port() {
    for _ in $(seq 1 60); do
        (echo > "/dev/tcp/127.0.0.1/$PORT") 2>/dev/null && return 0
        sleep 0.5
    done
    echo "port $PORT never opened" >&2
    return 1
}

run_server() {
    echo "== WebSocketServer vs Autobahn fuzzingclient"
    "$BIN/SNFWebSocketEchoServer" "$PORT" &
    local pid=$!
    trap 'kill $pid 2>/dev/null || true' RETURN
    wait_port
    docker run "${docker_args[@]}" "$IMAGE" wstest -m fuzzingclient -s /config/fuzzingclient.json >/dev/null
    python3 "$HERE/summarize.py" "$HERE/reports/server/index.json"
}

run_client() {
    echo "== WebSocket client vs Autobahn fuzzingserver"
    local name=snf-autobahn-fuzzingserver
    docker rm -f "$name" >/dev/null 2>&1 || true
    docker run -d --name "$name" "${docker_args[@]}" "$IMAGE" \
        wstest -m fuzzingserver -s /config/fuzzingserver.json >/dev/null
    trap "docker rm -f $name >/dev/null 2>&1 || true" RETURN
    wait_port
    timeout 1200 "$BIN/SNFWebSocketAutobahnClient" "$PORT" >/dev/null
    python3 "$HERE/summarize.py" "$HERE/reports/client/index.json"
}

# Run every requested side even if one fails, then report the combined result.
status=0
case "$MODE" in
    server) run_server || status=1 ;;
    client) run_client || status=1 ;;
    all) run_server || status=1; run_client || status=1 ;;
    *) echo "unknown mode $MODE" >&2; exit 2 ;;
esac
[ "$status" -eq 0 ] && echo "Autobahn: all cases passed" || echo "Autobahn: FAILED cases found" >&2
exit "$status"
