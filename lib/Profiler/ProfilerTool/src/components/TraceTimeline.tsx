import React, { useMemo } from 'react';
import type { SpanMessage } from '../hooks/useProfilerSocket';

interface Props { spans: SpanMessage[]; }

interface CompleteSpan {
  tid: number;
  cat: string;
  name: string;
  beginNs: number;
  endNs: number;
}

function hashColor(s: string): string {
  let h = 0;
  for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
  return `hsl(${h % 360},60%,55%)`;
}

function buildSpans(msgs: SpanMessage[]): CompleteSpan[] {
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
        complete.push({ tid: m.tid, cat: begin.cat, name: begin.name, beginNs: begin.ts, endNs: m.ts });
      }
    } else if (m.ph === 'X') {
      complete.push({ tid: m.tid, cat: m.cat, name: m.name, beginNs: m.ts, endNs: m.ts });
    }
  }
  return complete;
}

const ROW_HEIGHT = 24;
const LABEL_W    = 80;
const MIN_RECT_W = 2;

export const TraceTimeline: React.FC<Props> = ({ spans }) => {
  const complete = useMemo(() => buildSpans(spans), [spans]);

  const tids = useMemo(() =>
    [...new Set(complete.map(s => s.tid))].sort((a, b) => a - b),
    [complete]);

  const minTs = useMemo(() =>
    complete.length ? Math.min(...complete.map(s => s.beginNs)) : 0,
    [complete]);

  const maxTs = useMemo(() =>
    complete.length ? Math.max(...complete.map(s => s.endNs)) : 1,
    [complete]);

  const rangeNs = maxTs - minTs || 1;

  const svgWidth  = 900;
  const chartW    = svgWidth - LABEL_W - 8;
  const svgHeight = Math.max(tids.length * ROW_HEIGHT + 20, 60);

  if (!complete.length) {
    return <div style={{ padding: 16, color: '#888' }}>No trace data yet…</div>;
  }

  return (
    <svg width={svgWidth} height={svgHeight} style={{ display: 'block', fontFamily: 'monospace', fontSize: 11 }}>
      {tids.map((tid, row) => {
        const y = row * ROW_HEIGHT + 10;
        const rowSpans = complete.filter(s => s.tid === tid);
        return (
          <g key={tid}>
            <text x={LABEL_W - 4} y={y + ROW_HEIGHT / 2 + 4} textAnchor="end" fill="#ccc">
              T{tid.toString(16).slice(-4)}
            </text>
            {rowSpans.map((s, i) => {
              const x  = LABEL_W + ((s.beginNs - minTs) / rangeNs) * chartW;
              const w  = Math.max(MIN_RECT_W, ((s.endNs - s.beginNs) / rangeNs) * chartW);
              const us = ((s.endNs - s.beginNs) / 1000).toFixed(1);
              return (
                <rect key={i} x={x} y={y} width={w} height={ROW_HEIGHT - 2}
                      fill={hashColor(s.cat)} rx={2} opacity={0.85}>
                  <title>{s.name} [{s.cat}] tid={s.tid} {us}µs</title>
                </rect>
              );
            })}
          </g>
        );
      })}
    </svg>
  );
};
