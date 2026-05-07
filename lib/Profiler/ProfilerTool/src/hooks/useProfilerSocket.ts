import { useState, useEffect, useRef, useCallback } from 'react';

export interface SpanMessage {
  type: 'span';
  ts: number;
  tid: number;
  cat: string;
  name: string;
  ph: 'B' | 'E' | 'X' | 'I';
}

export interface MemMessage {
  type: 'mem';
  ts: number;
  live: number;
  bytes: number;
  peak: number;
}

export interface SysMessage {
  type: 'sys';
  ts: number;
  cpu: number[];
  ram_used: number;
  ram_free: number;
  net: Record<string, { rx_Bps: number; tx_Bps: number }>;
}

export interface UseProfilerSocketResult {
  spans: SpanMessage[];
  memSamples: MemMessage[];
  sysSamples: SysMessage[];
  connected: boolean;
}

const MAX_SPANS   = 8000;
const MAX_SAMPLES = 300;
const WS_URL      = 'ws://localhost:8765';
const FLUSH_MS    = 100;   // batch messages → max 10 React re-renders/sec

export function useProfilerSocket(): UseProfilerSocketResult {
  const [spans,      setSpans]      = useState<SpanMessage[]>([]);
  const [memSamples, setMemSamples] = useState<MemMessage[]>([]);
  const [sysSamples, setSysSamples] = useState<SysMessage[]>([]);
  const [connected,  setConnected]  = useState(false);

  const wsRef      = useRef<WebSocket | null>(null);
  const retryDelay = useRef(1000);
  const retryTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const mounted    = useRef(true);

  // Accumulate incoming messages here; drain every FLUSH_MS
  const spanBuf = useRef<SpanMessage[]>([]);
  const memBuf  = useRef<MemMessage[]>([]);
  const sysBuf  = useRef<SysMessage[]>([]);

  const connect = useCallback(() => {
    if (!mounted.current) return;
    const ws = new WebSocket(WS_URL);
    wsRef.current = ws;

    ws.onopen = () => {
      if (!mounted.current) { ws.close(); return; }
      setConnected(true);
      retryDelay.current = 1000;
    };

    ws.onmessage = (ev) => {
      if (!mounted.current) return;
      try {
        const msg = JSON.parse(ev.data as string) as SpanMessage | MemMessage | SysMessage | { type: string };
        if      (msg.type === 'span') spanBuf.current.push(msg as SpanMessage);
        else if (msg.type === 'mem')  memBuf.current.push(msg as MemMessage);
        else if (msg.type === 'sys')  sysBuf.current.push(msg as SysMessage);
      } catch { /* ignore malformed */ }
    };

    ws.onclose = () => {
      if (!mounted.current) return;
      setConnected(false);
      wsRef.current = null;
      retryTimer.current = setTimeout(() => {
        retryDelay.current = Math.min(retryDelay.current * 2, 30000);
        connect();
      }, retryDelay.current);
    };

    ws.onerror = () => { ws.close(); };
  }, []);

  // Flush buffers → state at a fixed cadence (not on every message)
  useEffect(() => {
    const id = setInterval(() => {
      if (!mounted.current) return;
      if (spanBuf.current.length) {
        const batch = spanBuf.current.splice(0);
        setSpans(prev => {
          const next = [...prev, ...batch];
          return next.length > MAX_SPANS ? next.slice(-MAX_SPANS) : next;
        });
      }
      if (memBuf.current.length) {
        const batch = memBuf.current.splice(0);
        setMemSamples(prev => {
          const next = [...prev, ...batch];
          return next.length > MAX_SAMPLES ? next.slice(-MAX_SAMPLES) : next;
        });
      }
      if (sysBuf.current.length) {
        const batch = sysBuf.current.splice(0);
        setSysSamples(prev => {
          const next = [...prev, ...batch];
          return next.length > MAX_SAMPLES ? next.slice(-MAX_SAMPLES) : next;
        });
      }
    }, FLUSH_MS);
    return () => clearInterval(id);
  }, []);

  useEffect(() => {
    mounted.current = true;
    connect();
    return () => {
      mounted.current = false;
      if (retryTimer.current) clearTimeout(retryTimer.current);
      wsRef.current?.close();
    };
  }, [connect]);

  return { spans, memSamples, sysSamples, connected };
}
