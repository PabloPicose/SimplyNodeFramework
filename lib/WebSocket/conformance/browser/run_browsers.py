"""Loads index.html in real browsers (Playwright) and runs interop.js against
an SNF WebSocket echo server, then checks the Origin allowlist: the page is
also served from a second origin (localhost:<http-port - 1>) that the echo
server does not allow, so its connection must be refused.
Usage: python run_browsers.py <ws-url> [http-port]"""
import functools, http.server, json, os, sys, threading
from playwright.sync_api import sync_playwright

ws_url = sys.argv[1] if len(sys.argv) > 1 else "ws://127.0.0.1:9001/"
http_port = int(sys.argv[2]) if len(sys.argv) > 2 else 8099
here = os.path.dirname(os.path.abspath(__file__))
handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=here)
httpd = http.server.ThreadingHTTPServer(("127.0.0.1", http_port), handler)
threading.Thread(target=httpd.serve_forever, daemon=True).start()
foreign = http.server.ThreadingHTTPServer(("127.0.0.1", http_port - 1), handler)
threading.Thread(target=foreign.serve_forever, daemon=True).start()

PROBE = """url => new Promise(resolve => {
  const ws = new WebSocket(url);
  ws.onopen = () => { ws.close(); resolve('open'); };
  ws.onclose = (e) => resolve('closed:' + e.code);
})"""

failed = False
with sync_playwright() as p:
    for name in ("chromium", "firefox", "webkit"):
        browser = getattr(p, name).launch()
        page = browser.new_page()
        page.goto(f"http://127.0.0.1:{http_port}/index.html")
        results = page.evaluate("url => runInterop(url)", ws_url)
        print(f"== {name} {browser.version}")
        for r in results:
            failed |= not r["ok"]
            print(f"  {'PASS' if r['ok'] else 'FAIL'}  {r['name']}" + (f"  ({r['detail']})" if r["detail"] else ""))
        page.goto(f"http://localhost:{http_port - 1}/index.html")
        outcome = page.evaluate(PROBE, ws_url)
        ok = outcome.startswith("closed")
        failed |= not ok
        print(f"  {'PASS' if ok else 'FAIL'}  foreign origin http://localhost:{http_port - 1} refused  ({outcome})")
        browser.close()
httpd.shutdown()
foreign.shutdown()
sys.exit(1 if failed else 0)
