import React, { useMemo, useState } from 'react';
import type { CompleteSpan } from '../spanUtils';
import { fmtNs, spanColor, spanKey } from '../spanUtils';

interface Props {
  completeSpans: CompleteSpan[];
  selectedSpan: CompleteSpan | null;
  onSelectSpan: (s: CompleteSpan | null) => void;
}

interface TooltipState {
  span: CompleteSpan;
  clientX: number;
  clientY: number;
}

const ROW_H    = 22;
const LABEL_W  = 88;
const MIN_W    = 2;
const PAD_TOP  = 6;
const PAD_BOT  = 8;
const SVG_W    = 900;

export const TraceTimeline: React.FC<Props> = ({ completeSpans, selectedSpan, onSelectSpan }) => {
  const [tooltip, setTooltip] = useState<TooltipState | null>(null);

  const tids = useMemo(() =>
    [...new Set(completeSpans.map(s => s.tid))].sort((a, b) => a - b),
    [completeSpans]);

  const maxDepthPerTid = useMemo(() => {
    const m = new Map<number, number>();
    for (const s of completeSpans) m.set(s.tid, Math.max(m.get(s.tid) ?? 0, s.depth));
    return m;
  }, [completeSpans]);

  // cumulative base Y for each thread section
  const threadBaseY = useMemo(() => {
    const base = new Map<number, number>();
    let y = 0;
    for (const tid of tids) {
      base.set(tid, y);
      const maxD = maxDepthPerTid.get(tid) ?? 0;
      y += PAD_TOP + (maxD + 1) * ROW_H + PAD_BOT;
    }
    return base;
  }, [tids, maxDepthPerTid]);

  const minTs = useMemo(() =>
    completeSpans.length ? Math.min(...completeSpans.map(s => s.beginNs)) : 0,
    [completeSpans]);

  const maxTs = useMemo(() =>
    completeSpans.length ? Math.max(...completeSpans.map(s => s.endNs)) : 1,
    [completeSpans]);

  const rangeNs = maxTs - minTs || 1;
  const chartW  = SVG_W - LABEL_W - 8;

  const svgHeight = useMemo(() => {
    let h = 0;
    for (const tid of tids) {
      const maxD = maxDepthPerTid.get(tid) ?? 0;
      h += PAD_TOP + (maxD + 1) * ROW_H + PAD_BOT;
    }
    return Math.max(h + 4, 60);
  }, [tids, maxDepthPerTid]);

  const selectedKey = selectedSpan ? spanKey(selectedSpan) : null;

  if (!completeSpans.length) {
    return <div style={{ padding: 16, color: '#888' }}>No trace data yet…</div>;
  }

  return (
    <div style={{ position: 'relative', display: 'inline-block' }}>
      <svg
        width={SVG_W}
        height={svgHeight}
        style={{ display: 'block', fontFamily: 'monospace', fontSize: 11 }}
        onClick={() => onSelectSpan(null)}
      >
        {tids.map((tid) => {
          const baseY   = threadBaseY.get(tid) ?? 0;
          const maxD    = maxDepthPerTid.get(tid) ?? 0;
          const sectH   = PAD_TOP + (maxD + 1) * ROW_H + PAD_BOT;
          const rowSpans = completeSpans.filter(s => s.tid === tid);

          return (
            <g key={tid}>
              {/* thread band background */}
              <rect x={LABEL_W} y={baseY} width={chartW} height={sectH - PAD_BOT / 2}
                fill="#0d1b2a" rx={2} />

              {/* thread label */}
              <text x={LABEL_W - 4} y={baseY + PAD_TOP + ROW_H * 0.7}
                textAnchor="end" fill="#64748b" fontSize={10}>
                T{tid.toString(16).slice(-4).toUpperCase()}
              </text>

              {/* separator */}
              <line x1={0} y1={baseY + sectH - PAD_BOT / 2}
                x2={SVG_W} y2={baseY + sectH - PAD_BOT / 2}
                stroke="#1e293b" strokeWidth={1} />

              {rowSpans.map((s, i) => {
                const x  = LABEL_W + ((s.beginNs - minTs) / rangeNs) * chartW;
                const w  = Math.max(MIN_W, ((s.endNs - s.beginNs) / rangeNs) * chartW);
                const sy = baseY + PAD_TOP + s.depth * ROW_H;
                const color = spanColor(s.cat, s.name);
                const key   = spanKey(s);
                const sel   = key === selectedKey;

                return (
                  <g key={i}>
                    <rect
                      x={x} y={sy} width={w} height={ROW_H - 2}
                      fill={color}
                      rx={2}
                      opacity={sel ? 1.0 : 0.82}
                      stroke={sel ? '#fff' : 'none'}
                      strokeWidth={sel ? 1.5 : 0}
                      style={{ cursor: 'pointer' }}
                      onMouseEnter={e => setTooltip({ span: s, clientX: e.clientX, clientY: e.clientY })}
                      onMouseMove={e  => setTooltip(t => t ? { ...t, clientX: e.clientX, clientY: e.clientY } : null)}
                      onMouseLeave={  () => setTooltip(null)}
                      onClick={e      => { e.stopPropagation(); onSelectSpan(sel ? null : s); }}
                    />
                    {w > 28 && (
                      <text
                        x={x + 3} y={sy + ROW_H * 0.68}
                        fill="rgba(0,0,0,0.75)" fontSize={10}
                        style={{ pointerEvents: 'none', userSelect: 'none' }}
                      >
                        {s.name.slice(0, Math.floor((w - 6) / 6.5))}
                      </text>
                    )}
                  </g>
                );
              })}
            </g>
          );
        })}
      </svg>

      {tooltip && (
        <SpanTooltip tooltip={tooltip} />
      )}
    </div>
  );
};

const SpanTooltip: React.FC<{ tooltip: TooltipState }> = ({ tooltip }) => {
  const { span, clientX, clientY } = tooltip;
  const duration = span.endNs - span.beginNs;
  // flip left if close to right edge
  const left = clientX + 16 + 260 > window.innerWidth
    ? clientX - 260 - 8
    : clientX + 16;

  return (
    <div style={{
      position: 'fixed',
      left,
      top: clientY - 8,
      background: '#1e293b',
      border: '1px solid #334155',
      borderRadius: 7,
      padding: '9px 13px',
      fontSize: 12,
      fontFamily: 'monospace',
      color: '#e2e8f0',
      pointerEvents: 'none',
      zIndex: 9999,
      width: 252,
      boxShadow: '0 6px 24px rgba(0,0,0,0.55)',
    }}>
      <div style={{ fontWeight: 700, color: '#38bdf8', marginBottom: 6, wordBreak: 'break-all' }}>
        {span.name}
      </div>
      <TooltipRow label="category" value={span.cat} />
      <TooltipRow label="thread"   value={`T${span.tid.toString(16).slice(-4).toUpperCase()}`} />
      <TooltipRow label="depth"    value={String(span.depth)} />
      <TooltipRow label="duration" value={fmtNs(duration)} valueColor="#4ade80" />
      <div style={{ marginTop: 7, color: '#475569', fontSize: 10 }}>
        click to inspect · click again to deselect
      </div>
    </div>
  );
};

const TooltipRow: React.FC<{ label: string; value: string; valueColor?: string }> = ({ label, value, valueColor }) => (
  <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 3 }}>
    <span style={{ color: '#64748b' }}>{label}</span>
    <span style={{ color: valueColor ?? '#cbd5e1' }}>{value}</span>
  </div>
);
