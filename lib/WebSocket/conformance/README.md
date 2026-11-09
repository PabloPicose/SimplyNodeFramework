# SNFWebSocket conformance and interop

Tools to check `SNFWebSocket` against third-party implementations. They are
not part of `ctest`: they need docker and take several minutes.

Build the drivers first:

```bash
cmake -B build/ws -DSNF_ENABLE_TESTS=ON -DSNF_ENABLE_WEBSOCKET_CONFORMANCE=ON
ninja -C build/ws SNFWebSocketEchoServer SNFWebSocketAutobahnClient
```

| Binary | Purpose |
|---|---|
| `SNFWebSocketEchoServer [port] [--allow-origin <origin>]...` | Echoes text/binary messages through `WebSocketServer`. |
| `SNFWebSocketAutobahnClient [port]` | Drives the Autobahn fuzzingserver with the `WebSocket` client. |

## Autobahn|Testsuite (RFC 6455)

```bash
lib/WebSocket/conformance/run_autobahn.sh build/ws          # server and client
lib/WebSocket/conformance/run_autobahn.sh build/ws server   # only WebSocketServer
```

Uses `crossbario/autobahn-testsuite` with `--network host` on port 9001.
HTML reports are written to `reports/{server,client}/index.html`; the script
prints a summary through `summarize.py` and exits non-zero if any case
FAILED, so it can gate CI. A Release build runs both sides in under a minute.

CI: `.github/workflows/websocket-conformance.yml` runs this on pushes and pull
requests that touch `lib/WebSocket/**` or `lib/Network/**`, and on demand
(`workflow_dispatch`). The reports are uploaded as the `autobahn-reports`
artifact. Sections 12 and 13
(permessage-deflate) are excluded because compression is not implemented.

Expected result on both sides: every case `OK`, except

- `6.4.3`, `6.4.4` NON-STRICT: invalid UTF-8 is detected when a frame is
  complete, not in the middle of a frame that arrives in several TCP chunks.
- `7.1.6`, `7.13.1`, `7.13.2` INFORMATIONAL: behavior not defined by the RFC.

## Browsers (Chromium, Firefox, WebKit)

Local only (the Playwright image is several GB, so it is not part of CI).

```bash
lib/WebSocket/conformance/browser/run_browsers.sh build/ws
```

Runs `browser/interop.js` in the three Playwright browsers (docker image
`mcr.microsoft.com/playwright/python`) against the echo server: UTF-8 text,
binary, 16/64-bit lengths (20 MiB), bursts, close codes and parallel
connections. It also serves the page from a second origin that the echo
server does not allow and checks that the browser connection is refused
(`WebSocketServer::setAllowedOrigins`).

`interop.js` also runs in Node >= 22 without docker:

```bash
build/ws/lib/WebSocket/conformance/SNFWebSocketEchoServer 9001 &
node lib/WebSocket/conformance/browser/interop.js ws://127.0.0.1:9001/
```
