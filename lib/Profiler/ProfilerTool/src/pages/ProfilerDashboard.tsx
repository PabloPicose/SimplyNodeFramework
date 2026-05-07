import React from 'react';
import { useProfilerSocket } from '../hooks/useProfilerSocket';
import { TraceTimeline } from '../components/TraceTimeline';
import { MemoryGraph } from '../components/MemoryGraph';
import { SysMonitor } from '../components/SysMonitor';

export const ProfilerDashboard: React.FC = () => {
  const { spans, memSamples, sysSamples, connected } = useProfilerSocket();

  return (
    <div style={{
      background: '#1a1a2e',
      minHeight: '100vh',
      padding: 16,
      color: '#e2e8f0',
      fontFamily: 'monospace',
    }}>
      {/* Header */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 16 }}>
        <h1 style={{ margin: 0, fontSize: 18, color: '#e2e8f0' }}>SNF Profiler</h1>
        <span style={{
          padding: '2px 10px',
          borderRadius: 12,
          fontSize: 12,
          background: connected ? '#064e3b' : '#7f1d1d',
          color:      connected ? '#34d399'  : '#fca5a5',
          border: `1px solid ${connected ? '#34d399' : '#f87171'}`,
        }}>
          {connected ? 'connected' : 'reconnecting…'}
        </span>
      </div>

      {/* Trace Timeline — full width */}
      <section style={{ marginBottom: 20 }}>
        <h2 style={{ fontSize: 13, color: '#94a3b8', margin: '0 0 6px' }}>Trace Timeline</h2>
        <div style={{ background: '#0f172a', borderRadius: 6, padding: 8, overflowX: 'auto' }}>
          <TraceTimeline spans={spans} />
        </div>
      </section>

      {/* Memory + Sys side by side */}
      <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap' }}>
        <section style={{ flex: '1 1 460px' }}>
          <h2 style={{ fontSize: 13, color: '#94a3b8', margin: '0 0 6px' }}>Memory</h2>
          <div style={{ background: '#0f172a', borderRadius: 6, padding: 8 }}>
            <MemoryGraph samples={memSamples} />
          </div>
        </section>

        <section style={{ flex: '1 1 360px' }}>
          <h2 style={{ fontSize: 13, color: '#94a3b8', margin: '0 0 6px' }}>System</h2>
          <div style={{ background: '#0f172a', borderRadius: 6, padding: 8 }}>
            <SysMonitor samples={sysSamples} />
          </div>
        </section>
      </div>
    </div>
  );
};

export default ProfilerDashboard;
