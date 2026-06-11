import React, { useEffect, useRef } from 'react';

export default function ConsoleLog({ logs }) {
  const ref = useRef(null);

  useEffect(() => {
    if (ref.current) {
      ref.current.scrollTop = ref.current.scrollHeight;
    }
  }, [logs?.length]);

  return (
    <div style={{
      height: 150,
      borderTop: '1px solid var(--border-color)',
      background: '#05070d',
      display: 'flex',
      flexDirection: 'column',
      flexShrink: 0
    }}>
      <div style={{
        height: 28,
        padding: '0 16px',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        borderBottom: '1px solid var(--border-color)',
        background: 'var(--bg-secondary)'
      }}>
        <div style={{
          fontSize: 10,
          textTransform: 'uppercase',
          letterSpacing: 1,
          color: 'var(--text-muted)',
          fontWeight: 600
        }}>
          📋 系统日志 | System Console
        </div>
        <div style={{
          display: 'flex',
          gap: 12,
          fontSize: 10,
          color: 'var(--text-muted)',
          fontFamily: 'Consolas,monospace'
        }}>
          <span style={{ color: 'var(--success)' }}>
            ✓ {logs?.filter(l => l.type === 'success').length || 0} OK
          </span>
          <span style={{ color: 'var(--warning)' }}>
            ! {logs?.filter(l => l.type === 'warn').length || 0} WARN
          </span>
          <span style={{ color: 'var(--error)' }}>
            ✗ {logs?.filter(l => l.type === 'error').length || 0} ERR
          </span>
          <span>
            共 {logs?.length || 0} 条
          </span>
        </div>
      </div>
      <div ref={ref} className="console-log" style={{ flex: 1 }}>
        {(!logs || logs.length === 0) ? (
          <div style={{ color: 'var(--text-muted)', fontSize: 10, padding: '8px 0' }}>
            等待系统事件...
          </div>
        ) : (
          logs.slice(-500).map(entry => (
            <div key={entry.id} className={`log-entry log-${entry.type}`}>
              <span className="log-time">[{entry.time}]</span>
              <span className="log-msg">{entry.msg}</span>
            </div>
          ))
        )}
      </div>
    </div>
  );
}
