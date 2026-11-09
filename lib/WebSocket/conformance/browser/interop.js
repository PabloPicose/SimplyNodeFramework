// Browser-API interop checks against an SNF WebSocket echo server.
// Runs unchanged in a browser page (window) and in Node >= 22 (global WebSocket).
async function runInterop(url) {
  const results = [];
  const check = (name, ok, detail = '') => results.push({ name, ok: !!ok, detail: String(detail) });

  const open = () => new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error('connect failed'));
  });
  const nextMessage = (ws) => new Promise((resolve) => { ws.onmessage = (e) => resolve(e.data); });
  const closed = (ws) => new Promise((resolve) => { ws.onclose = (e) => resolve(e); });
  const timeout = (p, ms, what) => Promise.race([p, new Promise((_, r) => setTimeout(() => r(new Error(what + ' timed out')), ms))]);

  try {
    const ws = await timeout(open(), 3000, 'open');
    check('open', ws.readyState === WebSocket.OPEN);

    const unicode = 'hola ñ κόσμε \u{1F600} \u0000 fin';
    ws.send(unicode);
    check('unicode text echo', (await timeout(nextMessage(ws), 3000, 'text')) === unicode);

    ws.send('');
    check('empty text echo', (await timeout(nextMessage(ws), 3000, 'empty')) === '');

    const bin = new Uint8Array(70000);
    for (let i = 0; i < bin.length; ++i) bin[i] = (i * 31) & 0xff;
    ws.send(bin);
    const binBack = new Uint8Array(await timeout(nextMessage(ws), 3000, 'binary'));
    check('binary 70000 B echo (16-bit len)', binBack.length === bin.length && binBack.every((v, i) => v === bin[i]));

    const big = 'x'.repeat(20 * 1024 * 1024);
    const t0 = Date.now();
    ws.send(big);
    const bigBack = await timeout(nextMessage(ws), 15000, 'big');
    check('text 20 MiB echo (64-bit len)', bigBack.length === big.length && bigBack === big, `${Date.now() - t0} ms`);

    const n = 2000;
    const received = [];
    const all = new Promise((resolve) => { ws.onmessage = (e) => { received.push(e.data); if (received.length === n) resolve(); }; });
    for (let i = 0; i < n; ++i) ws.send('m' + i);
    await timeout(all, 10000, 'burst');
    check(`burst of ${n} messages in order`, received.every((m, i) => m === 'm' + i));

    const closeEvent = closed(ws);
    ws.close(4001, 'adiós');
    const ev = await timeout(closeEvent, 7000, 'close');
    check('client close 4001 is clean and echoed', ev.wasClean && ev.code === 4001, `code=${ev.code} clean=${ev.wasClean}`);

    // Several simultaneous connections. Chromium serializes WebSocket
    // handshakes per endpoint and backs off after a burst (seconds), so the
    // budget here is about the browser policy, not the server, which answers
    // each Upgrade in well under 1 ms.
    const sockets = await Promise.all(Array.from({ length: 20 }, () => timeout(open(), 15000, 'open N')));
    const echoes = await Promise.all(sockets.map((s, i) => { const p = nextMessage(s); s.send('c' + i); return timeout(p, 3000, 'echo N'); }));
    check('20 parallel connections', echoes.every((m, i) => m === 'c' + i));
    await Promise.all(sockets.map((s) => { const p = closed(s); s.close(); return timeout(p, 7000, 'close N'); }));
  } catch (e) {
    check('exception', false, e.message);
  }
  return results;
}

if (typeof module !== 'undefined' && require.main === module) {
  runInterop(process.argv[2] || 'ws://127.0.0.1:9001/').then((results) => {
    for (const r of results) console.log(`${r.ok ? 'PASS' : 'FAIL'}  ${r.name}${r.detail ? '  (' + r.detail + ')' : ''}`);
    process.exit(results.every((r) => r.ok) ? 0 : 1);
  });
}
