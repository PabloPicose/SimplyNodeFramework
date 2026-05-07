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

const ROW_H       = 22;
const LABEL_W     = 88;
const MIN_W       = 2;
const PAD_TOP     = 6;
const PAD_BOT     = 8;
const RULER_H     = 20;   // time axis strip at top of SVG
const OVERVIEW_H  = 14;   // mini full-range strip at bottom
const SVG_W       = 900;
const ZOOM_MAX    = 50;

// Returns a "nice" tick interval for ~6 ticks across viewRangeNs.
function niceInterval(viewRangeNs: number): number {
  const rough = viewRangeNs / 6;
  const mag   = Math.pow(10, Math.floor(Math.log10(Math.max(rough, 1))));
  const norm  = rough / mag;
  const nice  = norm < 2 ? 1 : norm < 5 ? 2 : 5;
  return nice * mag;
}

export const TraceTimeline: React.FC<Props> = ({ completeSpans, selectedSpan, onSelectSpan }) => {
  const [tooltip, setTooltip]   = useState<TooltipState | null>(null);
  const [zoom,    setZoom]      = useState(1);    // 1 = show all; ZOOM_MAX = show 1/ZOOM_MAX
  const [panPct,  setPanPct]    = useState(1.0);  // 0 = oldest, 1 = newest (live)

  // ── Derived time range ───────────────────────────────────────────────────
  const tids = useMemo(() =>
    [...new Set(completeSpans.map(s => s.tid))].sort((a, b) => a - b),
    [completeSpans]);

  const maxDepthPerTid = useMemo(() => {
    const m = new Map<number, number>();
    for (const s of completeSpans) m.set(s.tid, Math.max(m.get(s.tid) ?? 0, s.depth));
    return m;
  }, [completeSpans]);

  const minTs = useMemo(() =>
    completeSpans.length ? Math.min(...completeSpans.map(s => s.beginNs)) : 0,
    [completeSpans]);
  const maxTs = useMemo(() =>
    completeSpans.length ? Math.max(...completeSpans.map(s => s.endNs)) : 1,
    [completeSpans]);
  const totalRangeNs = maxTs - minTs || 1;

  // View window: when pan is at 1 (or zoom=1) → auto-follow the live edge
  const canPan       = zoom > 1 + 1e-9;
  const autoFollow   = !canPan || panPct >= 1.0;
  const viewDurNs    = totalRangeNs / zoom;
  const viewStartNs  = autoFollow
    ? Math.max(minTs, maxTs - viewDurNs)
    : minTs + panPct * Math.max(0, totalRangeNs - viewDurNs);
  const viewEndNs    = viewStartNs + viewDurNs;
  const viewRangeNs  = viewEndNs - viewStartNs || 1;

  // ── Layout ───────────────────────────────────────────────────────────────
  const chartW = SVG_W - LABEL_W - 8;

  const threadBaseY = useMemo(() => {
    const base = new Map<number, number>();
    let y = RULER_H;
    for (const tid of tids) {
      base.set(tid, y);
      const maxD = maxDepthPerTid.get(tid) ?? 0;
      y += PAD_TOP + (maxD + 1) * ROW_H + PAD_BOT;
    }
    return base;
  }, [tids, maxDepthPerTid]);

  const svgHeight = useMemo(() => {
    let h = RULER_H + OVERVIEW_H + 2; // ruler + overview strip
    for (const tid of tids) {
      const maxD = maxDepthPerTid.get(tid) ?? 0;
      h += PAD_TOP + (maxD + 1) * ROW_H + PAD_BOT;
    }
    return Math.max(h, 60);
  }, [tids, maxDepthPerTid]);

  const contentH = svgHeight - RULER_H - OVERVIEW_H - 2; // thread rows height
  const overviewY = RULER_H + contentH + 2;              // Y of overview strip

  // ── Time ruler ticks ────────────────────────────────────────────────────
  const ticks = useMemo(() => {
    const interval   = niceInterval(viewRangeNs);
    const firstTick  = Math.ceil(viewStartNs / interval) * interval;
    const result: number[] = [];
    for (let t = firstTick; t <= viewEndNs + interval; t += interval) result.push(t);
    return result;
  }, [viewStartNs, viewEndNs, viewRangeNs]);

  // Map a timestamp → SVG x inside the chart
  const xForTs = (ts: number) => LABEL_W + ((ts - viewStartNs) / viewRangeNs) * chartW;

  // Map a timestamp → x in the overview strip (uses full totalRangeNs)
  const xOverview = (ts: number) => LABEL_W + ((ts - minTs) / totalRangeNs) * chartW;

  const selectedKey = selectedSpan ? spanKey(selectedSpan) : null;

  if (!completeSpans.length) {
    return <div style={{ padding: 16, color: '#888' }}>No trace data yet…</div>;
  }

  return (
    <div style={{ position: 'relative', display: 'inline-block', userSelect: 'none' }}>

      {/* ── SVG + vertical zoom slider ────────────────────────────────── */}
      <div style={{ display: 'flex', alignItems: 'flex-start', gap: 6 }}>
        <svg
          width={SVG_W}
          height={svgHeight}
          style={{ display: 'block', fontFamily: 'monospace', fontSize: 11, flexShrink: 0 }}
          onClick={() => onSelectSpan(null)}
        >
          <defs>
            {/* Clip the chart area so spans and grid lines don't bleed into labels */}
            <clipPath id="snf-chart-clip">
              <rect x={LABEL_W} y={RULER_H} width={chartW + 1} height={contentH} />
            </clipPath>
            <clipPath id="snf-ruler-clip">
              <rect x={LABEL_W} y={0} width={chartW + 1} height={RULER_H} />
            </clipPath>
          </defs>

          {/* ── Ruler background ────────────────────────────────────── */}
          <rect x={0} y={0} width={SVG_W} height={RULER_H} fill="#0a1628" />
          <line x1={LABEL_W} y1={RULER_H - 1} x2={SVG_W} y2={RULER_H - 1}
            stroke="#334155" strokeWidth={1} />

          {/* Tick grid + labels (clipped to chart + ruler) */}
          <g clipPath="url(#snf-ruler-clip)">
            {ticks.map(t => {
              const tx = xForTs(t);
              return (
                <text key={t} x={tx + 3} y={RULER_H - 5}
                  fill="#475569" fontSize={9}>
                  +{fmtNs(t - minTs)}
                </text>
              );
            })}
          </g>
          <g clipPath="url(#snf-chart-clip)">
            {ticks.map(t => {
              const tx = xForTs(t);
              return (
                <line key={t} x1={tx} y1={RULER_H} x2={tx} y2={RULER_H + contentH}
                  stroke="#1e293b" strokeWidth={1} strokeDasharray="2,5" />
              );
            })}
          </g>

          {/* ── Thread band backgrounds + labels ────────────────────── */}
          {tids.map(tid => {
            const baseY = threadBaseY.get(tid) ?? 0;
            const maxD  = maxDepthPerTid.get(tid) ?? 0;
            const sectH = PAD_TOP + (maxD + 1) * ROW_H + PAD_BOT;
            return (
              <g key={`bg-${tid}`}>
                <rect x={LABEL_W} y={baseY} width={chartW} height={sectH - PAD_BOT / 2}
                  fill="#0d1b2a" rx={2} />
                <line x1={0} y1={baseY + sectH - PAD_BOT / 2}
                  x2={SVG_W} y2={baseY + sectH - PAD_BOT / 2}
                  stroke="#1e293b" strokeWidth={1} />
                <text x={LABEL_W - 4} y={baseY + PAD_TOP + ROW_H * 0.7}
                  textAnchor="end" fill="#64748b" fontSize={10}>
                  T{tid.toString(16).slice(-4).toUpperCase()}
                </text>
              </g>
            );
          })}

          {/* ── Span rectangles (clipped to chart area) ─────────────── */}
          <g clipPath="url(#snf-chart-clip)">
            {tids.map(tid => {
              const baseY    = threadBaseY.get(tid) ?? 0;
              const rowSpans = completeSpans.filter(s =>
                s.tid === tid && s.endNs >= viewStartNs && s.beginNs <= viewEndNs);
              return (
                <g key={`spans-${tid}`}>
                  {rowSpans.map((s, i) => {
                    const sx    = xForTs(s.beginNs);
                    const sw    = Math.max(MIN_W, ((s.endNs - s.beginNs) / viewRangeNs) * chartW);
                    const sy    = baseY + PAD_TOP + s.depth * ROW_H;
                    const color = spanColor(s.cat, s.name);
                    const key   = spanKey(s);
                    const sel   = key === selectedKey;
                    return (
                      <g key={i}>
                        <rect
                          x={sx} y={sy} width={sw} height={ROW_H - 2}
                          fill={color} rx={2}
                          opacity={sel ? 1.0 : 0.82}
                          stroke={sel ? '#fff' : 'none'}
                          strokeWidth={sel ? 1.5 : 0}
                          style={{ cursor: 'pointer' }}
                          onMouseEnter={e => setTooltip({ span: s, clientX: e.clientX, clientY: e.clientY })}
                          onMouseMove={e  => setTooltip(t => t ? { ...t, clientX: e.clientX, clientY: e.clientY } : null)}
                          onMouseLeave={  () => setTooltip(null)}
                          onClick={e      => { e.stopPropagation(); onSelectSpan(sel ? null : s); }}
                        />
                        {sw > 28 && (
                          <text x={sx + 3} y={sy + ROW_H * 0.68}
                            fill="rgba(0,0,0,0.75)" fontSize={10}
                            style={{ pointerEvents: 'none', userSelect: 'none' }}>
                            {s.name.slice(0, Math.floor((sw - 6) / 6.5))}
                          </text>
                        )}
                      </g>
                    );
                  })}
                </g>
              );
            })}
          </g>

          {/* ── Overview strip ──────────────────────────────────────── */}
          <rect x={0} y={overviewY} width={SVG_W} height={OVERVIEW_H} fill="#060d1a" />
          <line x1={0} y1={overviewY} x2={SVG_W} y2={overviewY} stroke="#1e293b" strokeWidth={1} />

          {/* All spans as tiny marks in the overview */}
          {completeSpans.map((s, i) => {
            const ox = xOverview(s.beginNs);
            const ow = Math.max(1, ((s.endNs - s.beginNs) / totalRangeNs) * chartW);
            return (
              <rect key={i} x={ox} y={overviewY + 2} width={ow} height={OVERVIEW_H - 4}
                fill={spanColor(s.cat, s.name)} opacity={0.6} />
            );
          })}

          {/* Viewport indicator rectangle on overview */}
          {canPan && (() => {
            const vx = xOverview(viewStartNs);
            const vw = Math.max(4, ((viewEndNs - viewStartNs) / totalRangeNs) * chartW);
            return (
              <>
                {/* Dim areas outside viewport */}
                <rect x={LABEL_W} y={overviewY} width={Math.max(0, vx - LABEL_W)} height={OVERVIEW_H}
                  fill="rgba(0,0,0,0.55)" />
                <rect x={vx + vw} y={overviewY} width={Math.max(0, LABEL_W + chartW - vx - vw)} height={OVERVIEW_H}
                  fill="rgba(0,0,0,0.55)" />
                {/* Viewport border */}
                <rect x={vx} y={overviewY} width={vw} height={OVERVIEW_H}
                  fill="none" stroke="#38bdf8" strokeWidth={1.5} rx={1} />
              </>
            );
          })()}
        </svg>

        {/* ── Vertical zoom slider ───────────────────────────────────────── */}
        <div style={{
          display: 'flex', flexDirection: 'column',
          alignItems: 'center', justifyContent: 'space-between',
          width: 24, height: svgHeight,
          paddingTop: RULER_H, paddingBottom: OVERVIEW_H + 2,
          boxSizing: 'border-box',
        }}>
          <span style={{ fontSize: 9, color: '#475569', lineHeight: 1 }}>+</span>
          <div style={{
            flex: 1, display: 'flex', alignItems: 'center',
            justifyContent: 'center', overflow: 'hidden',
          }}>
            <input
              type="range" min={1} max={ZOOM_MAX} step={0.5} value={zoom}
              title={`Zoom: ${zoom.toFixed(1)}×`}
              style={{
                width: contentH - 16,
                height: 20,
                transform: 'rotate(90deg)',
                cursor: 'pointer',
                accentColor: '#38bdf8',
                flexShrink: 0,
              }}
              onChange={e => {
                const z = Number(e.target.value);
                setZoom(z);
                if (z <= 1 + 1e-9) setPanPct(1); // reset to live when fully zoomed out
              }}
            />
          </div>
          <span style={{ fontSize: 9, color: '#475569', lineHeight: 1 }}>−</span>
        </div>
      </div>

      {/* ── Horizontal pan slider ─────────────────────────────────────────── */}
      <div style={{ paddingLeft: LABEL_W, paddingRight: 30, marginTop: 4 }}>
        <input
          type="range" min={0} max={1000}
          value={Math.round(panPct * 1000)}
          disabled={!canPan}
          style={{
            width: '100%', display: 'block',
            cursor: canPan ? 'pointer' : 'default',
            accentColor: '#38bdf8',
            opacity: canPan ? 1 : 0.35,
          }}
          onChange={e => setPanPct(Number(e.target.value) / 1000)}
        />
        <div style={{
          display: 'flex', justifyContent: 'space-between',
          fontSize: 10, color: '#475569', marginTop: 2,
        }}>
          <span>+{fmtNs(viewStartNs - minTs)}</span>
          <span style={{ color: autoFollow ? '#4ade80' : '#94a3b8' }}>
            {autoFollow ? '● live' : `${zoom.toFixed(1)}×  ·  ${fmtNs(viewRangeNs)} window`}
          </span>
          <span>+{fmtNs(viewEndNs - minTs)}</span>
        </div>
      </div>

      {/* ── Tooltip ─────────────────────────────────────────────────────────── */}
      {tooltip && <SpanTooltip tooltip={tooltip} />}
    </div>
  );
};

// ─── sub-components ──────────────────────────────────────────────────────────

const SpanTooltip: React.FC<{ tooltip: TooltipState }> = ({ tooltip }) => {
  const { span, clientX, clientY } = tooltip;
  const duration = span.endNs - span.beginNs;
  const left = clientX + 16 + 260 > window.innerWidth ? clientX - 268 : clientX + 16;
  return (
    <div style={{
      position: 'fixed', left, top: clientY - 8,
      background: '#1e293b', border: '1px solid #334155',
      borderRadius: 7, padding: '9px 13px',
      fontSize: 12, fontFamily: 'monospace', color: '#e2e8f0',
      pointerEvents: 'none', zIndex: 9999, width: 252,
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
