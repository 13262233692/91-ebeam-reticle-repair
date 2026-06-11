import React, { useState } from 'react';
import { formatArea, formatLength, formatDwellTime, formatDose } from '../utils/svgUtils.js';

export default function DefectPanel(props) {
  const {
    detectionResult, selectedDefectId, hoveredDefectId,
    onDefectClick, onDefectHover,
    scanPathResult, doseMatrixResult
  } = props;

  const [tab, setTab] = useState('defects');
  const [filterType, setFilterType] = useState('all');
  const [sortBy, setSortBy] = useState('area');
  const [search, setSearch] = useState('');

  if (!detectionResult) {
    return (
      <aside className="panel">
        <div className="panel-header">
          <div className="panel-title">缺陷与指令 | Defects & Commands</div>
          <div className="panel-badge">0</div>
        </div>
        <div className="panel-content">
          <div className="empty-placeholder">
            <div className="empty-placeholder-icon">📋</div>
            <div>尚未运行缺陷检测</div>
            <div style={{ marginTop: 6 }}>加载图像后点击「缺陷检测」以开始分析</div>
          </div>
          
          <div className="section" style={{ marginTop: 20 }}>
            <div className="section-title">图例 | Legend</div>
            <div className="legend">
              <div className="legend-item">
                <span className="legend-swatch opaque"></span>
                <span><b style={{ color: 'var(--opaque)' }}>Opaque Defect</b><br /><span style={{ fontSize: 10, color: 'var(--text-muted)' }}>多余金属异常</span></span>
              </div>
              <div className="legend-item">
                <span className="legend-swatch clear"></span>
                <span><b style={{ color: 'var(--clear)' }}>Clear Defect</b><br /><span style={{ fontSize: 10, color: 'var(--text-muted)' }}>金属缺失异常</span></span>
              </div>
              <div className="legend-item">
                <span className="legend-swatch scan"></span>
                <span><b style={{ color: 'var(--warning)' }}>Raster Scan Line</b><br /><span style={{ fontSize: 10, color: 'var(--text-muted)' }}>往复扫描路径</span></span>
              </div>
              <div className="legend-item">
                <span className="legend-swatch beam"></span>
                <span><b style={{ color: 'var(--accent-primary)' }}>E-Beam Exposure</b><br /><span style={{ fontSize: 10, color: 'var(--text-muted)' }}>电子束曝光点</span></span>
              </div>
            </div>
          </div>
        </div>
      </aside>
    );
  }

  const allDefects = [...detectionResult.opaqueDefects.map(d => ({ ...d, _type: 'opaque' })), ...detectionResult.clearDefects.map(d => ({ ...d, _type: 'clear' }))];

  let filtered = allDefects;
  if (filterType !== 'all') {
    filtered = filtered.filter(d => d._type === filterType);
  }
  if (search) {
    const s = search.toLowerCase();
    filtered = filtered.filter(d => d.id.toLowerCase().includes(s));
  }
  filtered.sort((a, b) => {
    if (sortBy === 'area') return b.area - a.area;
    if (sortBy === 'width') return (b.boundingBox?.width || 0) - (a.boundingBox?.width || 0);
    if (sortBy === 'height') return (b.boundingBox?.height || 0) - (a.boundingBox?.height || 0);
    return 0;
  });

  const selected = allDefects.find(d => d.id === selectedDefectId);
  const selScanLines = scanPathResult?.scanLines?.filter(l => l.points?.some(p => p.defectId === selectedDefectId)) || [];
  const selScanPoints = selScanLines.reduce((s, l) => s + (l.points?.length || 0), 0);
  const selCommands = doseMatrixResult?.commands?.filter(c => c.defectId === selectedDefectId) || [];

  return (
    <aside className="panel">
      <div className="panel-header">
        <div className="panel-title">缺陷与指令 | Defects & Commands</div>
        <div className="panel-badge">{allDefects.length}</div>
      </div>

      <div className="panel-content">
        <div className="tab-bar">
          <button className={`tab-btn ${tab === 'defects' ? 'active' : ''}`} onClick={() => setTab('defects')}>
            缺陷列表 ({allDefects.length})
          </button>
          <button className={`tab-btn ${tab === 'details' ? 'active' : ''}`} onClick={() => setTab('details')}>
            详情 {selected ? '· ' + selected.id : ''}
          </button>
          <button className={`tab-btn ${tab === 'commands' ? 'active' : ''}`} onClick={() => setTab('commands')}>
            指令队列 ({doseMatrixResult?.stats?.totalCommands || 0})
          </button>
        </div>

        {tab === 'defects' && (
          <>
            <div className="form-row" style={{ marginBottom: 8 }}>
              <div className="form-group" style={{ marginBottom: 0 }}>
                <select className="form-select" value={filterType} onChange={e => setFilterType(e.target.value)}>
                  <option value="all">全部类型</option>
                  <option value="opaque">仅 Opaque</option>
                  <option value="clear">仅 Clear</option>
                </select>
              </div>
              <div className="form-group" style={{ marginBottom: 0 }}>
                <select className="form-select" value={sortBy} onChange={e => setSortBy(e.target.value)}>
                  <option value="area">按面积</option>
                  <option value="width">按宽度</option>
                  <option value="height">按高度</option>
                </select>
              </div>
            </div>
            <div className="form-group">
              <input
                type="text"
                className="form-input"
                placeholder="搜索缺陷 ID..."
                value={search}
                onChange={e => setSearch(e.target.value)}
              />
            </div>

            <div style={{ maxHeight: 380, overflowY: 'auto', paddingRight: 4 }}>
              {filtered.length === 0 ? (
                <div className="empty-placeholder">
                  <div className="empty-placeholder-icon">🔍</div>
                  <div>没有匹配的缺陷</div>
                </div>
              ) : (
                filtered.map(defect => {
                  const isSel = defect.id === selectedDefectId;
                  const isHov = defect.id === hoveredDefectId;
                  return (
                    <div
                      key={defect.id}
                      className={`defect-item ${isSel ? 'selected' : ''}`}
                      onClick={() => onDefectClick(defect.id)}
                      onMouseEnter={() => onDefectHover(defect.id)}
                      onMouseLeave={() => onDefectHover(null)}
                      style={{
                        borderColor: isHov && !isSel ? 'var(--border-light)' : undefined
                      }}
                    >
                      <div className="defect-item-header">
                        <div className={`defect-type defect-type-${defect._type}`}>
                          {defect._type === 'opaque' ? 'Opaque' : 'Clear'}
                        </div>
                        <div className="defect-id">{defect.id}</div>
                      </div>
                      <div className="defect-stats">
                        <div className="defect-stat">面积: <span>{formatArea(defect.area)}</span></div>
                        <div className="defect-stat">图层: <span>{defect.layer ?? 0}</span></div>
                        <div className="defect-stat">
                          尺寸: <span>
                            {defect.boundingBox ? `${formatLength(defect.boundingBox.width)} × ${formatLength(defect.boundingBox.height)}` : '--'}
                          </span>
                        </div>
                        <div className="defect-stat">
                          中心: <span>
                            {defect.centroid ? `(${defect.centroid.x.toFixed(0)}, ${defect.centroid.y.toFixed(0)})` : '--'}
                          </span>
                        </div>
                      </div>
                    </div>
                  );
                })
              )}
            </div>
          </>
        )}

        {tab === 'details' && (
          !selected ? (
            <div className="empty-placeholder">
              <div className="empty-placeholder-icon">🎯</div>
              <div>请选择一个缺陷查看详情</div>
              <div style={{ marginTop: 6, fontSize: 10 }}>点击缺陷列表或图像中的缺陷轮廓</div>
            </div>
          ) : (
            <>
              <div className="stats-grid">
                <div className={`stat-card ${selected._type === 'opaque' ? 'opaque' : 'clear'}`}>
                  <div className="stat-value">{selected._type === 'opaque' ? 'OP' : 'CL'}</div>
                  <div className="stat-label">缺陷类型</div>
                </div>
                <div className="stat-card">
                  <div className="stat-value">{selected.layer ?? 0}</div>
                  <div className="stat-label">图层索引</div>
                </div>
                <div className="stat-card" style={{ gridColumn: 'span 2' }}>
                  <div className="stat-value" style={{ fontSize: 14 }}>{formatArea(selected.area)}</div>
                  <div className="stat-label">缺陷面积 / Defect Area</div>
                </div>
              </div>

              <div className="section">
                <div className="section-title">几何参数 | Geometry</div>
                <div className="form-row">
                  <div className="form-group">
                    <label className="form-label">宽 (X 方向)</label>
                    <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                      {selected.boundingBox ? formatLength(selected.boundingBox.width) : '--'}
                    </div>
                  </div>
                  <div className="form-group">
                    <label className="form-label">高 (Y 方向)</label>
                    <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                      {selected.boundingBox ? formatLength(selected.boundingBox.height) : '--'}
                    </div>
                  </div>
                </div>
                <div className="form-row">
                  <div className="form-group">
                    <label className="form-label">中心 Cx</label>
                    <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                      {selected.centroid?.x?.toFixed(2) || '--'} px
                    </div>
                  </div>
                  <div className="form-group">
                    <label className="form-label">中心 Cy</label>
                    <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                      {selected.centroid?.y?.toFixed(2) || '--'} px
                    </div>
                  </div>
                </div>
                <div className="form-row">
                  <div className="form-group">
                    <label className="form-label">轮廓点数</label>
                    <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                      {selected.points?.length || '--'}
                    </div>
                  </div>
                  <div className="form-group">
                    <label className="form-label">长宽比</label>
                    <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                      {selected.boundingBox && selected.boundingBox.height > 0
                        ? (selected.boundingBox.width / selected.boundingBox.height).toFixed(2)
                        : '--'}
                    </div>
                  </div>
                </div>
              </div>

              <div className="section">
                <div className="section-title">扫描与剂量 | Scan & Dose</div>
                <div className="form-row">
                  <div className="form-group">
                    <label className="form-label">扫描行数</label>
                    <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                      {selScanLines.length}
                    </div>
                  </div>
                  <div className="form-group">
                    <label className="form-label">曝光点数</label>
                    <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                      {selScanPoints.toLocaleString()}
                    </div>
                  </div>
                </div>
                {selCommands.length > 0 && (
                  <>
                    <div className="form-row">
                      <div className="form-group">
                        <label className="form-label">指令条数</label>
                        <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                          {selCommands.length.toLocaleString()}
                        </div>
                      </div>
                      <div className="form-group">
                        <label className="form-label">对应总剂量</label>
                        <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                          {formatDose(selCommands.reduce((s, c) => s + (c.dose || 0), 0))}
                        </div>
                      </div>
                    </div>
                  </>
                )}
              </div>

              <div className="section">
                <div className="section-title">推荐工艺 | Recommended Process</div>
                <div className="legend">
                  <div style={{ fontSize: 11, color: selected._type === 'opaque' ? 'var(--opaque)' : 'var(--clear)' }}>
                    {selected._type === 'opaque'
                      ? '▶ Opaque 缺陷 (多余金属): 推荐减材刻蚀工艺'
                      : '▶ Clear 缺陷 (金属缺失): 推荐增材沉积工艺'}
                  </div>
                  <div className="legend-item">
                    <span style={{ color: 'var(--text-muted)' }}>能量密度:</span>
                    <span style={{ fontFamily: 'Consolas,monospace' }}>{selected._type === 'opaque' ? '高' : '中'}</span>
                  </div>
                  <div className="legend-item">
                    <span style={{ color: 'var(--text-muted)' }}>预计刻蚀深度:</span>
                    <span style={{ fontFamily: 'Consolas,monospace' }}>{selected._type === 'opaque' ? '25 nm' : '15 nm'}</span>
                  </div>
                </div>
              </div>
            </>
          )
        )}

        {tab === 'commands' && (
          !doseMatrixResult ? (
            <div className="empty-placeholder">
              <div className="empty-placeholder-icon">⚡</div>
              <div>尚未生成剂量矩阵</div>
              <div style={{ marginTop: 6, fontSize: 10 }}>点击「构建剂量矩阵」生成 E-Beam 指令</div>
            </div>
          ) : (
            <>
              <div className="stats-grid" style={{ marginBottom: 12 }}>
                <div className="stat-card scan">
                  <div className="stat-value">{doseMatrixResult.stats.totalCommands.toLocaleString()}</div>
                  <div className="stat-label">指令总数</div>
                </div>
                <div className="stat-card dose">
                  <div className="stat-value">{formatDose(doseMatrixResult.stats.totalDose)}</div>
                  <div className="stat-label">总剂量</div>
                </div>
              </div>
              <div className="form-row" style={{ marginBottom: 10 }}>
                <div className="form-group" style={{ marginBottom: 0 }}>
                  <label className="form-label">最大停留时间</label>
                  <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                    {formatDwellTime(doseMatrixResult.stats.maxDwellTime)}
                  </div>
                </div>
                <div className="form-group" style={{ marginBottom: 0 }}>
                  <label className="form-label">平均停留</label>
                  <div className="form-input" style={{ fontFamily: 'Consolas,monospace', background: 'var(--bg-primary)' }}>
                    {formatDwellTime(doseMatrixResult.stats.avgDwellTime)}
                  </div>
                </div>
              </div>

              <div className="section-title" style={{ marginTop: 4 }}>指令预览 (前 500 条)</div>
              <div className="command-list">
                <div className="command-header">
                  <div>#</div>
                  <div className="command-cell x">X</div>
                  <div className="command-cell y">Y</div>
                  <div className="command-cell v">Vd(kV)</div>
                  <div className="command-cell d">Dw(ns)</div>
                  <div>Mat</div>
                </div>
                {doseMatrixResult.commands.slice(0, 500).map((c, i) => (
                  <div key={i} className="command-row">
                    <div className="command-cell">{i}</div>
                    <div className="command-cell x">{c.x.toFixed(1)}</div>
                    <div className="command-cell y">{c.y.toFixed(1)}</div>
                    <div className="command-cell v">{c.xVoltage.toFixed(2)}</div>
                    <div className="command-cell d">{c.dwellTime}</div>
                    <div className="command-cell" style={{
                      color: c.material === 'opaque' ? 'var(--opaque)' : c.material === 'clear' ? 'var(--clear)' : 'var(--accent-primary)',
                      fontSize: 9
                    }}>
                      {c.material === 'opaque' ? 'OP' : c.material === 'clear' ? 'CL' : c.material === 'border' ? 'BD' : 'TG'}
                    </div>
                  </div>
                ))}
              </div>
              {doseMatrixResult.commands.length > 500 && (
                <div style={{ textAlign: 'center', padding: '8px 0', fontSize: 10, color: 'var(--text-muted)' }}>
                  仅显示前 500 条，共 {doseMatrixResult.stats.totalCommands.toLocaleString()} 条指令
                </div>
              )}
            </>
          )
        )}
      </div>
    </aside>
  );
}
