import React, { useMemo } from 'react';
import type { MemMessage } from '../hooks/useProfilerSocket';

interface Props { samples: MemMessage[]; }

function fmtBytes(n: number): string {
  if (n >= 1e9) return (n / 1e9).toFixed(1) + ' GB';
  if (n >= 1e6) return (n / 1e6).toFixed(1) + ' MB';
  if (n >= 1e3) return (n / 1e3).toFixed(1) + ' KB';
  return n + ' B';
}

function polyline(pts: Array<[number, number]>): string {
  return pts.map(([x, y]) => `${x.toFixed(1)},${y.toFixed(1)}`).join(' ');
}

const W = 500, H = 140, PAD_L = 60, PAD_B = 20, PAD_T = 10, PAD_R = 10;
const CW = W - PAD_L - PAD_R;
const CH = H - PAD_T - PAD_B;

export const MemoryGraph: React.FC<Props> = ({ samples }) => {
  const { livePoints, peakPoints, maxBytes } = useMemo(() => {
    if (!samples.length) return { livePoints: [], peakPoints: [], maxBytes: 1 };
    const minTs   = samples[0].ts;
    const maxTs   = samples[samples.length - 1].ts;
    const rangeTs = maxTs - minTs || 1;
    const maxBytes = Math.max(...samples.map(s => s.peak), 1);
    const toX = (ts: number) => PAD_L + ((ts - minTs) / rangeTs) * CW;
    const toY = (v: number)  => PAD_T + CH - (v / maxBytes) * CH;
    return {
      livePoints: samples.map(s => [toX(s.ts), toY(s.bytes)] as [number, number]),
      peakPoints: samples.map(s => [toX(s.ts), toY(s.peak)]  as [number, number]),
      maxBytes,
    };
  }, [samples]);

  if (!samples.length) {
    return <div style={{ padding: 16, color: '#888' }}>No memory data yet…</div>;
  }

  return (
    <svg width={W} height={H} style={{ display: 'block', fontFamily: 'monospace', fontSize: 10 }}>
      {/* axes */}
      <line x1={PAD_L} y1={PAD_T} x2={PAD_L} y2={PAD_T + CH} stroke="#555" strokeWidth={1}/>
      <line x1={PAD_L} y1={PAD_T + CH} x2={PAD_L + CW} y2={PAD_T + CH} stroke="#555" strokeWidth={1}/>
      {/* Y labels */}
      <text x={PAD_L - 4} y={PAD_T + 4}          textAnchor="end" fill="#888">{fmtBytes(maxBytes)}</text>
      <text x={PAD_L - 4} y={PAD_T + CH / 2 + 4} textAnchor="end" fill="#888">{fmtBytes(maxBytes / 2)}</text>
      <text x={PAD_L - 4} y={PAD_T + CH + 4}      textAnchor="end" fill="#888">0</text>
      {/* lines */}
      <polyline points={polyline(peakPoints)} fill="none" stroke="#f87171" strokeWidth={1.5} strokeDasharray="4 2"/>
      <polyline points={polyline(livePoints)} fill="none" stroke="#34d399" strokeWidth={2}/>
      {/* legend */}
      <line x1={PAD_L + 8}  y1={PAD_T + 12} x2={PAD_L + 28} y2={PAD_T + 12} stroke="#34d399" strokeWidth={2}/>
      <text x={PAD_L + 32} y={PAD_T + 16} fill="#ccc">live</text>
      <line x1={PAD_L + 72} y1={PAD_T + 12} x2={PAD_L + 92} y2={PAD_T + 12} stroke="#f87171" strokeWidth={1.5} strokeDasharray="4 2"/>
      <text x={PAD_L + 96} y={PAD_T + 16} fill="#ccc">peak</text>
    </svg>
  );
};
