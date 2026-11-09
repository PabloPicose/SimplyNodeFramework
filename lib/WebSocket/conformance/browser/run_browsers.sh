#!/usr/bin/env bash
# Runs interop.js in Chromium, Firefox and WebKit (Playwright, docker) against
# the SNF echo server.   ./run_browsers.sh <build-dir>
set -euo pipefail
BUILD_DIR=$(realpath "${1:?usage: $0 <build-dir>}")
HERE=$(cd "$(dirname "$0")" && pwd)
PW_VERSION=1.64.1
"$BUILD_DIR/lib/WebSocket/conformance/SNFWebSocketEchoServer" 9001 \
    --allow-origin http://127.0.0.1:8099 >/dev/null &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
sleep 0.3
docker run --rm --network host --ipc=host -v "$HERE:/work:ro" \
    "mcr.microsoft.com/playwright/python:v${PW_VERSION}-noble" \
    bash -c "pip install -q --break-system-packages playwright==${PW_VERSION} >/dev/null 2>&1 &&
             python /work/run_browsers.py ws://127.0.0.1:9001/ 2>&1 | grep -v ' - - '"
