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

const ROW_H          = 22;
const LABEL_W        = 88;
const MIN_W          = 2;
const PAD_TOP        = 6;
const PAD_BOT        = 8;
const RULER_H        = 20;
const OVERVIEW_H     = 14;
const VISIBLE_H      = 370;   // fixed SVG viewport height
const SVG_W          = 900;
const ZOOM_MAX       = 50;
const SLIDER_COL_W   = 26;    // width of each right-side slider column

// Content visible height = room between ruler and overview strip
const CONTENT_VIS_H  = VISIBLE_H - RULER_H - OVERVIEW_H - 4;

function niceInterval(viewRangeNs: number): number {
  const rough = viewRangeNs / 6;
  const mag   = Math.pow(10, Math.floor(Math.log10(Math.max(rough, 1))));
  const norm  = rough / mag;
  return (norm < 2 ? 1 : norm < 5 ? 2 : 5) * mag;
}

export const TraceTimeline: React.FC<Props> = ({ completeSpans, selectedSpan, onSelectSpan }) => {
  const [tooltip,    setTooltip]   = useState<TooltipState | null>(null);
  const [zoom,       setZoom]      = useState(1);    // 1=all; ZOOM_MAX=1/ZOOM_MAX of range
  const [panPct,     setPanPct]    = useState(1.0);  // 0=oldest 1=live
  const [vertOffset, setVertOffset] = useState(0);   // px scroll into thread rows

  // ── Derived data ─────────────────────────────────────────────────────────
  const tids = useMemo(() =>
    [...new Set(completeSpans.map(s => s.tid))].sort((a, b) => a - b),
    [completeSpans]);

  const maxDepthPerTid = useMemo(() => {
    const m = new Map<number, number>();
    for (const s of completeSpans) m.set(s.tid, Math.max(m.get(s.tid) ?? 0, s.depth));
    return m;
  }, [completeSpans]);

  // threadBaseY — y=0 is the top of the scrollable content group (not SVG root)
  const { threadBaseY, contentH } = useMemo(() => {
    const base = new Map<number, number>();
    let y = 0;
    for (const tid of tids) {
      base.set(tid, y);
      const maxD = maxDepthPerTid.get(tid) ?? 0;
      y += PAD_TOP + (maxD + 1) * ROW_H + PAD_BOT;
    }
    return { threadBaseY: base, contentH: y };
  }, [tids, maxDepthPerTid]);

  const maxVertOffset = Math.max(0, contentH - CONTENT_VIS_H);
  // clamp vertOffset if content shrank (new data collapsed a thread)
  const safeVertOffset = Math.min(vertOffset, maxVertOffset);

  const minTs = useMemo(() =>
    completeSpans.length ? Math.min(...completeSpans.map(s => s.beginNs)) : 0,
    [completeSpans]);
  const maxTs = useMemo(() =>
    completeSpans.length ? Math.max(...completeSpans.map(s => s.endNs)) : 1,
    [completeSpans]);
  const totalRangeNs = maxTs - minTs || 1;

  // ── Time viewport ─────────────────────────────────────────────────────────
  const canPan      = zoom > 1 + 1e-9;
  const autoFollow  = !canPan || panPct >= 1.0;
  const viewDurNs   = totalRangeNs / zoom;
  const viewStartNs = autoFollow
    ? Math.max(minTs, maxTs - viewDurNs)
    : minTs + panPct * Math.max(0, totalRangeNs - viewDurNs);
  const viewEndNs   = viewStartNs + viewDurNs;
  const viewRangeNs = viewEndNs - viewStartNs || 1;

  const chartW = SVG_W - LABEL_W - 8;

  // ── Ruler ticks ───────────────────────────────────────────────────────────
  const ticks = useMemo(() => {
    const interval  = niceInterval(viewRangeNs);
    const firstTick = Math.ceil(viewStartNs / interval) * interval;
    const result: number[] = [];
    for (let t = firstTick; t <= viewEndNs + interval; t += interval) result.push(t);
    return result;
  }, [viewStartNs, viewEndNs, viewRangeNs]);

  const xForTs       = (ts: number) => LABEL_W + ((ts - viewStartNs) / viewRangeNs) * chartW;
  const xForTsFull   = (ts: number) => LABEL_W + ((ts - minTs) / totalRangeNs) * chartW;
  const selectedKey  = selectedSpan ? spanKey(selectedSpan) : null;

  // SVG y-coordinates for fixed sections
  const contentGroupY = RULER_H - safeVertOffset;  // translate so rows appear in RULER_H..RULER_H+CONTENT_VIS_H
  const overviewY     = VISIBLE_H - OVERVIEW_H - 1;

  if (!completeSpans.length) {
    return <div style={{ padding: 16, color: '#888' }}>No trace data yet…</div>;
  }

  const canScroll = maxVertOffset > 0;

  return (
    <div style={{ position: 'relative', display: 'inline-block', userSelect: 'none' }}>

      {/* ── SVG + right-side sliders ──────────────────────────────────── */}
      <div style={{ display: 'flex', alignItems: 'flex-start', gap: 4 }}>

        <svg
          width={SVG_W}
          height={VISIBLE_H}
          style={{ display: 'block', fontFamily: 'monospace', fontSize: 11, flexShrink: 0 }}
          onClick={() => onSelectSpan(null)}
        >
          <defs>
            {/* clips span rects to chart area (no label bleed, no overflow past overview) */}
            <clipPath id="snf-chart-clip">
              <rect x={LABEL_W} y={RULER_H} width={chartW + 1} height={CONTENT_VIS_H} />
            </clipPath>
            {/* clips ruler labels */}
            <clipPath id="snf-ruler-clip">
              <rect x={LABEL_W} y={0} width={chartW + 1} height={RULER_H} />
            </clipPath>
            {/* clips scrollable thread content (backgrounds + spans + labels) */}
            <clipPath id="snf-content-clip">
              <rect x={0} y={RULER_H} width={SVG_W} height={CONTENT_VIS_H} />
            </clipPath>
          </defs>

          {/* ── Ruler ──────────────────────────────────────────────── */}
          <rect x={0} y={0} width={SVG_W} height={RULER_H} fill="#0a1628" />
          <line x1={LABEL_W} y1={RULER_H - 1} x2={SVG_W} y2={RULER_H - 1}
            stroke="#334155" strokeWidth={1} />
          <g clipPath="url(#snf-ruler-clip)">
            {ticks.map(t => (
              <text key={t} x={xForTs(t) + 3} y={RULER_H - 5} fill="#475569" fontSize={9}>
                +{fmtNs(t - minTs)}
              </text>
            ))}
          </g>

          {/* ── Tick grid lines (clipped to chart area) ─────────────── */}
          <g clipPath="url(#snf-chart-clip)">
            {ticks.map(t => (
              <line key={t}
                x1={xForTs(t)} y1={RULER_H}
                x2={xForTs(t)} y2={RULER_H + CONTENT_VIS_H}
                stroke="#1e293b" strokeWidth={1} strokeDasharray="2,5" />
            ))}
          </g>

          {/* ── Thread content (scrolls vertically) ─────────────────── */}
          <g clipPath="url(#snf-content-clip)">
            <g transform={`translate(0, ${contentGroupY})`}>

              {/* Thread band backgrounds + labels */}
              {tids.map(tid => {
                const baseY = threadBaseY.get(tid) ?? 0;
                const maxD  = maxDepthPerTid.get(tid) ?? 0;
                const sectH = PAD_TOP + (maxD + 1) * ROW_H + PAD_BOT;
                return (
                  <g key={`bg-${tid}`}>
                    <rect x={LABEL_W} y={baseY} width={chartW}
                      height={sectH - PAD_BOT / 2} fill="#0d1b2a" rx={2} />
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

              {/* Span rectangles (also clip to chart area x-axis) */}
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

            </g>
          </g>

          {/* ── Overview strip (fixed at bottom) ────────────────────── */}
          <rect x={0} y={overviewY} width={SVG_W} height={OVERVIEW_H + 1} fill="#060d1a" />
          <line x1={0} y1={overviewY} x2={SVG_W} y2={overviewY} stroke="#1e293b" strokeWidth={1} />
          {completeSpans.map((s, i) => (
            <rect key={i}
              x={xForTsFull(s.beginNs)}
              y={overviewY + 2}
              width={Math.max(1, ((s.endNs - s.beginNs) / totalRangeNs) * chartW)}
              height={OVERVIEW_H - 4}
              fill={spanColor(s.cat, s.name)} opacity={0.6} />
          ))}
          {canPan && (() => {
            const vx = xForTsFull(viewStartNs);
            const vw = Math.max(4, ((viewEndNs - viewStartNs) / totalRangeNs) * chartW);
            return (
              <>
                <rect x={LABEL_W} y={overviewY}
                  width={Math.max(0, vx - LABEL_W)} height={OVERVIEW_H}
                  fill="rgba(0,0,0,0.55)" />
                <rect x={vx + vw} y={overviewY}
                  width={Math.max(0, LABEL_W + chartW - vx - vw)} height={OVERVIEW_H}
                  fill="rgba(0,0,0,0.55)" />
                <rect x={vx} y={overviewY} width={vw} height={OVERVIEW_H}
                  fill="none" stroke="#38bdf8" strokeWidth={1.5} rx={1} />
              </>
            );
          })()}
        </svg>

        {/* ── Right-side sliders ─────────────────────────────────────── */}
        <div style={{ display: 'flex', gap: 4 }}>

          {/* Zoom (time axis) */}
          <SliderColumn
            label="Z" min={1} max={ZOOM_MAX} step={0.5}
            value={zoom} height={VISIBLE_H}
            title="Zoom time axis"
            onChange={z => { setZoom(z); if (z <= 1 + 1e-9) setPanPct(1); }}
          />

          {/* Vertical thread scroll */}
          <SliderColumn
            label="S" min={0} max={maxVertOffset} step={1}
            value={safeVertOffset} height={VISIBLE_H}
            disabled={!canScroll}
            title="Scroll threads"
            onChange={setVertOffset}
            inverted   /* top of slider = offset 0 = scroll to top */
          />
        </div>
      </div>

      {/* ── Horizontal pan slider ────────────────────────────────────── */}
      <div style={{ paddingLeft: LABEL_W, paddingRight: (SLIDER_COL_W + 4) * 2 + 8, marginTop: 4 }}>
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

      {tooltip && <SpanTooltip tooltip={tooltip} />}
    </div>
  );
};

// ─── SliderColumn ─────────────────────────────────────────────────────────────
// A labelled vertical range input in a fixed-width column.
interface SliderColumnProps {
  label: string;
  min: number; max: number; step: number; value: number;
  height: number;
  disabled?: boolean;
  title?: string;
  inverted?: boolean;   // flip so max is at top
  onChange: (v: number) => void;
}
const SliderColumn: React.FC<SliderColumnProps> = ({
  label, min, max, step, value, height, disabled, title, inverted, onChange,
}) => {
  const trackLen = height - 24; // leave 12px top + 12px bottom for labels
  // When inverted, we flip the value so the thumb at top = min offset = top of threads
  const sliderVal = inverted ? max - value + min : value;
  return (
    <div
      title={title}
      style={{
        width: SLIDER_COL_W,
        height,
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'space-between',
        opacity: disabled ? 0.3 : 1,
      }}
    >
      <span style={{ fontSize: 9, color: '#475569', lineHeight: 1, marginTop: 2 }}>
        {inverted ? '↑' : '+'}
      </span>
      <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', overflow: 'hidden' }}>
        <input
          type="range" min={min} max={max} step={step}
          value={sliderVal}
          disabled={disabled}
          style={{
            width: trackLen,
            height: 20,
            transform: 'rotate(90deg)',
            cursor: disabled ? 'default' : 'pointer',
            accentColor: inverted ? '#a78bfa' : '#38bdf8',
            flexShrink: 0,
          }}
          onChange={e => {
            const v = Number(e.target.value);
            onChange(inverted ? max - v + min : v);
          }}
        />
      </div>
      <span style={{ fontSize: 9, color: '#475569', lineHeight: 1, marginBottom: 2 }}>
        {inverted ? '↓' : '−'}
      </span>
    </div>
  );
};

// ─── SpanTooltip ──────────────────────────────────────────────────────────────
const SpanTooltip: React.FC<{ tooltip: TooltipState }> = ({ tooltip }) => {
  const { span, clientX, clientY } = tooltip;
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
      <TRow label="category" value={span.cat} />
      <TRow label="thread"   value={`T${span.tid.toString(16).slice(-4).toUpperCase()}`} />
      <TRow label="depth"    value={String(span.depth)} />
      <TRow label="duration" value={fmtNs(span.endNs - span.beginNs)} valueColor="#4ade80" />
      <div style={{ marginTop: 7, color: '#475569', fontSize: 10 }}>
        click to inspect · click again to deselect
      </div>
    </div>
  );
};

const TRow: React.FC<{ label: string; value: string; valueColor?: string }> = ({ label, value, valueColor }) => (
  <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 3 }}>
    <span style={{ color: '#64748b' }}>{label}</span>
    <span style={{ color: valueColor ?? '#cbd5e1' }}>{value}</span>
  </div>
);
