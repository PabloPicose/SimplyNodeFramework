import { useState, useEffect, useRef, useCallback } from 'react';

export interface SpanMessage {
  type: 'span';
  ts: number;      // nanoseconds
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

export function useProfilerSocket(): UseProfilerSocketResult {
  const [spans,      setSpans]      = useState<SpanMessage[]>([]);
  const [memSamples, setMemSamples] = useState<MemMessage[]>([]);
  const [sysSamples, setSysSamples] = useState<SysMessage[]>([]);
  const [connected,  setConnected]  = useState(false);

  const wsRef      = useRef<WebSocket | null>(null);
  const retryDelay = useRef(1000);
  const retryTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const mounted    = useRef(true);

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
        if (msg.type === 'span') {
          setSpans(prev => {
            const next = [...prev, msg as SpanMessage];
            return next.length > MAX_SPANS ? next.slice(next.length - MAX_SPANS) : next;
          });
        } else if (msg.type === 'mem') {
          setMemSamples(prev => {
            const next = [...prev, msg as MemMessage];
            return next.length > MAX_SAMPLES ? next.slice(next.length - MAX_SAMPLES) : next;
          });
        } else if (msg.type === 'sys') {
          setSysSamples(prev => {
            const next = [...prev, msg as SysMessage];
            return next.length > MAX_SAMPLES ? next.slice(next.length - MAX_SAMPLES) : next;
          });
        }
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
