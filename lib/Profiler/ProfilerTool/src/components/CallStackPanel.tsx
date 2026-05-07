import React, { useMemo } from 'react';
import type { CompleteSpan } from '../spanUtils';
import { fmtNs, spanColor, spanKey } from '../spanUtils';

interface Props {
  selected: CompleteSpan;
  allSpans: CompleteSpan[];
  onClose: () => void;
}

export const CallStackPanel: React.FC<Props> = ({ selected, allSpans, onClose }) => {
  const selKey = spanKey(selected);

  // Ancestors on the same thread that fully contain the selected span (excluding itself)
  const ancestors = useMemo(() =>
    allSpans
      .filter(s =>
        s.tid === selected.tid &&
        s.beginNs <= selected.beginNs &&
        s.endNs   >= selected.endNs &&
        spanKey(s) !== selKey,
      )
      // sort widest → narrowest (root first, direct parent last before selected)
      .sort((a, b) => (b.endNs - b.beginNs) - (a.endNs - a.beginNs)),
    [allSpans, selected, selKey],
  );

  // Direct children: same thread, contained within selected, one depth level deeper
  const children = useMemo(() =>
    allSpans
      .filter(s =>
        s.tid    === selected.tid &&
        s.depth  === selected.depth + 1 &&
        s.beginNs >= selected.beginNs &&
        s.endNs  <= selected.endNs &&
        spanKey(s) !== selKey,
      )
      .sort((a, b) => a.beginNs - b.beginNs),
    [allSpans, selected, selKey],
  );

  const duration = selected.endNs - selected.beginNs;

  return (
    <aside style={{
      background: '#0f172a',
      border: '1px solid #1e293b',
      borderRadius: 8,
      padding: 16,
      fontFamily: 'monospace',
      fontSize: 12,
      color: '#e2e8f0',
      width: 300,
      flexShrink: 0,
    }}>
      {/* Panel header */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 14 }}>
        <span style={{ fontSize: 13, fontWeight: 700, color: '#38bdf8' }}>Call Stack</span>
        <button
          onClick={onClose}
          style={{
            background: 'none', border: 'none',
            color: '#64748b', cursor: 'pointer',
            fontSize: 18, lineHeight: 1, padding: '0 2px',
          }}
          title="Close"
        >×</button>
      </div>

      {/* Selected span info card */}
      <div style={{
        background: '#1e293b',
        borderRadius: 6,
        padding: '10px 12px',
        marginBottom: 16,
        borderLeft: `3px solid ${spanColor(selected.cat, selected.name)}`,
      }}>
        <div style={{ fontWeight: 700, color: '#f8fafc', marginBottom: 8, wordBreak: 'break-all' }}>
          {selected.name}
        </div>
        <InfoRow label="category" value={selected.cat} />
        <InfoRow label="thread"   value={`T${selected.tid.toString(16).slice(-4).toUpperCase()}`} />
        <InfoRow label="depth"    value={String(selected.depth)} />
        <InfoRow label="duration" value={fmtNs(duration)} valueColor="#4ade80" />
      </div>

      {/* Call stack (ancestors + selected) */}
      <SectionLabel>Stack</SectionLabel>
      <div style={{ marginBottom: 16 }}>
        {ancestors.length === 0 && (
          <div style={{ color: '#475569', fontStyle: 'italic', fontSize: 11, marginBottom: 6 }}>
            top-level span
          </div>
        )}
        {ancestors.map((a, i) => (
          <StackFrame key={spanKey(a)} span={a} indent={i} />
        ))}
        <StackFrame span={selected} indent={ancestors.length} highlight />
      </div>

      {/* Children */}
      {children.length > 0 && (
        <>
          <SectionLabel>Children ({children.length})</SectionLabel>
          <div style={{ maxHeight: 220, overflowY: 'auto' }}>
            {children.map(c => (
              <ChildRow key={spanKey(c)} span={c} parentDuration={duration} />
            ))}
          </div>
        </>
      )}

      {children.length === 0 && (
        <div style={{ color: '#334155', fontStyle: 'italic', fontSize: 11 }}>no child spans</div>
      )}
    </aside>
  );
};

// ─── sub-components ──────────────────────────────────────────────────────────

const SectionLabel: React.FC<React.PropsWithChildren> = ({ children }) => (
  <div style={{
    fontSize: 10,
    color: '#475569',
    textTransform: 'uppercase',
    letterSpacing: '0.08em',
    marginBottom: 6,
  }}>
    {children}
  </div>
);

const InfoRow: React.FC<{ label: string; value: string; valueColor?: string }> = ({ label, value, valueColor }) => (
  <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 3 }}>
    <span style={{ color: '#64748b' }}>{label}</span>
    <span style={{ color: valueColor ?? '#cbd5e1' }}>{value}</span>
  </div>
);

const StackFrame: React.FC<{ span: CompleteSpan; indent: number; highlight?: boolean }> = ({ span, indent, highlight }) => {
  const color = spanColor(span.cat, span.name);
  return (
    <div style={{
      display: 'flex',
      alignItems: 'center',
      padding: '4px 7px',
      borderRadius: 4,
      marginBottom: 2,
      marginLeft: indent * 10,
      background: highlight ? '#172554' : '#1e293b',
      border: `1px solid ${highlight ? '#3b82f6' : 'transparent'}`,
      fontSize: 11,
    }}>
      <span style={{
        display: 'inline-block',
        width: 6, height: 6,
        borderRadius: '50%',
        background: color,
        flexShrink: 0,
        marginRight: 7,
      }} />
      <span style={{
        overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
        color: highlight ? '#93c5fd' : '#94a3b8',
        flex: 1,
      }}>
        {span.name}
      </span>
      <span style={{ color: '#4ade80', flexShrink: 0, marginLeft: 8, fontSize: 10 }}>
        {fmtNs(span.endNs - span.beginNs)}
      </span>
    </div>
  );
};

const ChildRow: React.FC<{ span: CompleteSpan; parentDuration: number }> = ({ span, parentDuration }) => {
  const duration = span.endNs - span.beginNs;
  const pct = parentDuration > 0 ? (duration / parentDuration) * 100 : 0;
  const color = spanColor(span.cat, span.name);

  return (
    <div style={{ marginBottom: 3 }}>
      <div style={{
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        padding: '3px 7px',
        borderRadius: 4,
        background: '#1e293b',
        fontSize: 11,
      }}>
        <span style={{
          display: 'inline-block',
          width: 6, height: 6,
          borderRadius: '50%',
          background: color,
          flexShrink: 0,
          marginRight: 7,
        }} />
        <span style={{
          overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
          color: '#94a3b8', flex: 1,
        }}>
          {span.name}
        </span>
        <span style={{ color: '#4ade80', flexShrink: 0, marginLeft: 8, fontSize: 10 }}>
          {fmtNs(duration)}
        </span>
      </div>
      {/* % of parent bar */}
      <div style={{ height: 2, background: '#0d1b2a', borderRadius: 1, marginTop: 1, marginLeft: 7 }}>
        <div style={{ height: '100%', width: `${Math.min(pct, 100).toFixed(1)}%`, background: color, borderRadius: 1 }} />
      </div>
    </div>
  );
};
