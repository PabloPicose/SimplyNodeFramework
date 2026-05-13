import React, { useMemo } from 'react';
import type { SysMessage } from '../hooks/useProfilerSocket';

interface Props { samples: SysMessage[]; }

function polyline(pts: Array<[number, number]>): string {
  return pts.map(([x, y]) => `${x.toFixed(1)},${y.toFixed(1)}`).join(' ');
}

const Sparkline: React.FC<{ values: number[]; label: string }> = ({ values, label }) => {
  const W = 120, H = 40, PAD = 4;
  const cw = W - PAD * 2, ch = H - PAD * 2;
  if (!values.length) return null;
  const pts = values.map((v, i): [number, number] => [
    PAD + (i / Math.max(values.length - 1, 1)) * cw,
    PAD + ch - v * ch,
  ]);
  const last = values[values.length - 1];
  return (
    <svg width={W} height={H} style={{ display: 'inline-block', margin: 2 }}>
      <text x={PAD} y={H - 2} fill="#888" fontSize={9}>{label}</text>
      <polyline points={polyline(pts)} fill="none" stroke="#60a5fa" strokeWidth={1.5}/>
      <text x={W - PAD} y={PAD + 10} textAnchor="end" fill="#60a5fa" fontSize={9}>
        {(last * 100).toFixed(0)}%
      </text>
    </svg>
  );
};

export const SysMonitor: React.FC<Props> = ({ samples }) => {
  const coreCount = useMemo(() =>
    samples.length ? samples[samples.length - 1].cpu.length : 0,
    [samples]);

  const cpuHistory = useMemo((): number[][] => {
    const cores: number[][] = Array.from({ length: coreCount }, () => []);
    for (const s of samples) s.cpu.forEach((v, i) => { if (cores[i]) cores[i].push(v); });
    return cores;
  }, [samples, coreCount]);

  const W = 300, H = 100, PL = 40, PB = 16, PT = 8, PR = 8;
  const CW = W - PL - PR, CH = H - PT - PB;

  const ramChart = useMemo(() => {
    if (!samples.length) return null;
    const minTs   = samples[0].ts, maxTs = samples[samples.length - 1].ts;
    const rangeTs = maxTs - minTs || 1;
    const maxRam  = Math.max(...samples.map(s => s.ram_used + s.ram_free), 1);
    const toX     = (ts: number) => PL + ((ts - minTs) / rangeTs) * CW;
    const usedPts  = samples.map(s => [toX(s.ts), PT + CH - (s.ram_used / maxRam) * CH] as [number, number]);
    const totalPts = samples.map(s => [toX(s.ts), PT + CH - ((s.ram_used + s.ram_free) / maxRam) * CH] as [number, number]);
    const gb = (n: number) => (n / 1e9).toFixed(1) + 'G';
    return { usedPts, totalPts, maxRam, gb };
  }, [samples]);

  const netChart = useMemo(() => {
    if (!samples.length) return null;
    const minTs   = samples[0].ts, maxTs = samples[samples.length - 1].ts;
    const rangeTs = maxTs - minTs || 1;
    const allRx   = samples.map(s => Object.values(s.net).reduce((a, n) => a + n.rx_Bps, 0));
    const allTx   = samples.map(s => Object.values(s.net).reduce((a, n) => a + n.tx_Bps, 0));
    const maxNet  = Math.max(...allRx, ...allTx, 1);
    const toX     = (i: number) => PL + (i / Math.max(samples.length - 1, 1)) * CW;
    const toY     = (v: number) => PT + CH - (v / maxNet) * CH;
    const rxPts   = allRx.map((v, i): [number, number] => [toX(i), toY(v)]);
    const txPts   = allTx.map((v, i): [number, number] => [toX(i), toY(v)]);
    const fmtRate = (n: number) => n >= 1e6 ? (n / 1e6).toFixed(1) + 'MB/s' : (n / 1e3).toFixed(1) + 'KB/s';
    return { rxPts, txPts, fmtRate, maxNet };
  }, [samples]);

  if (!samples.length) return <div style={{ padding: 16, color: '#888' }}>No system data yet…</div>;

  return (
    <div style={{ color: '#ccc', fontFamily: 'monospace', fontSize: 12 }}>
      {/* CPU */}
      <div style={{ marginBottom: 8 }}>
        <strong>CPU</strong>
        <div style={{ display: 'flex', flexWrap: 'wrap' }}>
          {cpuHistory.map((vals, i) => (
            <Sparkline key={i} values={vals} label={`core${i}`} />
          ))}
        </div>
      </div>

      {/* RAM */}
      <div style={{ marginBottom: 8 }}>
        <strong>RAM</strong>
        {ramChart && (
          <svg width={W} height={H} style={{ display: 'block' }}>
            <line x1={PL} y1={PT} x2={PL} y2={PT + CH} stroke="#555" strokeWidth={1}/>
            <line x1={PL} y1={PT + CH} x2={PL + CW} y2={PT + CH} stroke="#555" strokeWidth={1}/>
            <text x={PL - 2} y={PT + 10} textAnchor="end" fill="#888" fontSize={9}>{ramChart.gb(ramChart.maxRam)}</text>
            <polyline points={polyline(ramChart.totalPts)} fill="none" stroke="#555" strokeWidth={1} strokeDasharray="3 2"/>
            <polyline points={polyline(ramChart.usedPts)}  fill="none" stroke="#a78bfa" strokeWidth={2}/>
          </svg>
        )}
      </div>

      {/* Network */}
      <div>
        <strong>Network</strong>
        {netChart && (
          <svg width={W} height={H} style={{ display: 'block' }}>
            <line x1={PL} y1={PT} x2={PL} y2={PT + CH} stroke="#555" strokeWidth={1}/>
            <line x1={PL} y1={PT + CH} x2={PL + CW} y2={PT + CH} stroke="#555" strokeWidth={1}/>
            <text x={PL - 2} y={PT + 10} textAnchor="end" fill="#888" fontSize={9}>{netChart.fmtRate(netChart.maxNet)}</text>
            <polyline points={polyline(netChart.rxPts)} fill="none" stroke="#34d399" strokeWidth={1.5}/>
            <polyline points={polyline(netChart.txPts)} fill="none" stroke="#fb923c" strokeWidth={1.5}/>
            <text x={PL + 8}  y={PT + 16} fill="#34d399" fontSize={9}>↓ RX</text>
            <text x={PL + 48} y={PT + 16} fill="#fb923c" fontSize={9}>↑ TX</text>
          </svg>
        )}
      </div>
    </div>
  );
};
