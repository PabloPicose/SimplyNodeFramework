import type { SpanMessage } from './hooks/useProfilerSocket';

export interface CompleteSpan {
  tid: number;
  cat: string;
  name: string;
  beginNs: number;
  endNs: number;
  depth: number;
}

export function buildSpans(msgs: SpanMessage[]): CompleteSpan[] {
  const stacks = new Map<number, SpanMessage[]>();
  const complete: CompleteSpan[] = [];

  for (const m of msgs) {
    if (m.ph === 'B') {
      const st = stacks.get(m.tid) ?? [];
      st.push(m);
      stacks.set(m.tid, st);
    } else if (m.ph === 'E') {
      const st = stacks.get(m.tid);
      if (st && st.length > 0) {
        const begin = st.pop()!;
        // depth = stack length after pop = how deep this span is nested
        complete.push({
          tid: m.tid,
          cat: begin.cat,
          name: begin.name,
          beginNs: begin.ts,
          endNs: m.ts,
          depth: st.length,
        });
      }
    } else if (m.ph === 'X') {
      const st = stacks.get(m.tid) ?? [];
      complete.push({
        tid: m.tid,
        cat: m.cat,
        name: m.name,
        beginNs: m.ts,
        endNs: m.ts,
        depth: st.length,
      });
    }
  }
  return complete;
}

export function fmtNs(ns: number): string {
  if (ns < 1_000) return `${ns} ns`;
  if (ns < 1_000_000) return `${(ns / 1_000).toFixed(2)} µs`;
  if (ns < 1_000_000_000) return `${(ns / 1_000_000).toFixed(2)} ms`;
  return `${(ns / 1_000_000_000).toFixed(3)} s`;
}

export function spanColor(cat: string, name: string): string {
  let h = 0;
  const s = cat + name;
  for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
  return `hsl(${h % 360},55%,52%)`;
}

export function spanKey(s: CompleteSpan): string {
  return `${s.tid}:${s.beginNs}:${s.endNs}:${s.name}`;
}
