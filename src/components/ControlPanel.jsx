import React from 'react';
import { formatArea, formatDose, formatDwellTime } from '../utils/svgUtils.js';

export default function ControlPanel(props) {
  const {
    params, setParams,
    onLoadImage, onRunDetection, onGenerateScanPath, onBuildDoseMatrix,
    onExportCommands, onExportSvg,
    processing, progress,
    imageData, detectionResult, scanPathResult, doseMatrixResult,
    layers, currentLayerIdx, onLayerChange
  } = props;

  const updateParam = (key, val) => setParams(p => ({ ...p, [key]: val }));

  const totalDefects = detectionResult
    ? detectionResult.opaqueDefects.length + detectionResult.clearDefects.length
    : 0;

  return (
    <aside className="panel">
      <div className="panel-header">
        <div className="panel-title">控制面板 | Control Panel</div>
        <div className="panel-badge">
          {processing ? progress + '%' : 'READY'}
        </div>
      </div>

      <div className="panel-content">
        {processing && (
          <div className="progress-wrap">
            <div className="progress-bar">
              <div className="progress-fill" style={{ width: progress + '%' }}></div>
            </div>
            <div className="progress-info">
              <span>{processing}</span>
              <span>{progress}%</span>
            </div>
          </div>
        )}

        <div className="section">
          <div className="section-title">项目统计 | Overview</div>
          <div className="stats-grid">
            <div className="stat-card">
              <div className="stat-value">{imageData ? imageData.width + '×' + imageData.height : '--'}</div>
              <div className="stat-label">图像尺寸</div>
            </div>
            <div className="stat-card opaque">
              <div className="stat-value">{detectionResult ? detectionResult.opaqueDefects.length : '--'}</div>
              <div className="stat-label">Opaque 缺陷</div>
            </div>
            <div className="stat-card clear">
              <div className="stat-value">{detectionResult ? detectionResult.clearDefects.length : '--'}</div>
              <div className="stat-label">Clear 缺陷</div>
            </div>
            <div className="stat-card scan">
              <div className="stat-value">{scanPathResult ? scanPathResult.totalPoints.toLocaleString() : '--'}</div>
              <div className="stat-label">扫描点</div>
            </div>
            <div className="stat-card dose" style={{ gridColumn: 'span 2' }}>
              <div className="stat-value">{doseMatrixResult ? formatDose(doseMatrixResult.stats.totalDose) : '--'}</div>
              <div className="stat-label">总剂量 / Total Dose</div>
            </div>
          </div>
        </div>

        {layers.length > 1 && (
          <div className="section">
            <div className="section-title">图层 | Layers ({layers.length})</div>
            <div className="tabs-layer">
              {layers.map((layer, i) => (
                <button
                  key={i}
                  className={`layer-tab ${currentLayerIdx === i ? 'active' : ''}`}
                  onClick={() => onLayerChange(i)}
                >
                  L{i + 1}
                </button>
              ))}
            </div>
            <div style={{ fontSize: 10, color: 'var(--text-muted)', padding: '4px 0' }}>
              当前: {layers[currentLayerIdx]?.name || 'Layer ' + (currentLayerIdx + 1)}
            </div>
          </div>
        )}

        <div className="section">
          <div className="section-title">图像处理 | Detection Params</div>
          
          <div className="form-group">
            <label className="form-label">高斯滤波 σ (Gaussian Sigma): {params.gaussianSigma.toFixed(1)}</label>
            <input
              type="range" min="0.1" max="5" step="0.1"
              value={params.gaussianSigma}
              onChange={e => updateParam('gaussianSigma', parseFloat(e.target.value))}
              className="form-slider"
            />
            <div className="slider-value"><span>0.1</span><span>5.0</span></div>
          </div>

          <div className="form-row">
            <div className="form-group">
              <label className="form-label">Block Size</label>
              <input
                type="number" min="3" max="99" step="2"
                value={params.blockSize}
                onChange={e => updateParam('blockSize', Math.max(3, parseInt(e.target.value) || 31))}
                className="form-input"
              />
            </div>
            <div className="form-group">
              <label className="form-label">常数 C</label>
              <input
                type="number" min="-50" max="50" step="1"
                value={params.C}
                onChange={e => updateParam('C', parseInt(e.target.value) || 0)}
                className="form-input"
              />
            </div>
          </div>

          <div className="form-row">
            <div className="form-group">
              <label className="form-label">Canny 低阈值</label>
              <input
                type="number" min="1" max="255" step="1"
                value={params.cannyLow}
                onChange={e => updateParam('cannyLow', Math.min(255, Math.max(1, parseInt(e.target.value) || 40)))}
                className="form-input"
              />
            </div>
            <div className="form-group">
              <label className="form-label">Canny 高阈值</label>
              <input
                type="number" min="1" max="255" step="1"
                value={params.cannyHigh}
                onChange={e => updateParam('cannyHigh', Math.min(255, Math.max(1, parseInt(e.target.value) || 120)))}
                className="form-input"
              />
            </div>
          </div>

          <button
            className="btn-block btn-primary"
            onClick={onRunDetection}
            disabled={!imageData || processing}
          >
            🔍 运行缺陷检测 / Detect Defects
          </button>
        </div>

        <div className="section">
          <div className="section-title">扫描路径 | Scan Path</div>
          
          <div className="form-group">
            <label className="form-label">扫描模式</label>
            <select
              value={params.scanMode}
              onChange={e => updateParam('scanMode', e.target.value)}
              className="form-select"
            >
              <option value="raster">Raster Scan (往复扫描)</option>
              <option value="contour">Contour Scan (轮廓环绕)</option>
              <option value="hybrid">Hybrid (混合扫描)</option>
            </select>
          </div>

          <div className="form-row">
            <div className="form-group">
              <label className="form-label">X 步长 (px)</label>
              <input
                type="number" min="0.5" max="50" step="0.5"
                value={params.stepX}
                onChange={e => updateParam('stepX', Math.max(0.5, parseFloat(e.target.value) || 2))}
                className="form-input"
              />
            </div>
            <div className="form-group">
              <label className="form-label">Y 步长 (px)</label>
              <input
                type="number" min="0.5" max="50" step="0.5"
                value={params.stepY}
                onChange={e => updateParam('stepY', Math.max(0.5, parseFloat(e.target.value) || 2))}
                className="form-input"
              />
            </div>
          </div>

          <div className="form-group">
            <label className="form-label">扫描重叠率 Overlap: {(params.overlap * 100).toFixed(0)}%</label>
            <input
              type="range" min="0" max="0.8" step="0.05"
              value={params.overlap}
              onChange={e => updateParam('overlap', parseFloat(e.target.value))}
              className="form-slider"
            />
            <div className="slider-value"><span>0%</span><span>80%</span></div>
          </div>

          <button
            className="btn-block btn-secondary"
            onClick={onGenerateScanPath}
            disabled={!detectionResult || processing}
          >
            🛣️ 生成扫描路径 / Generate Path
          </button>
        </div>

        <div className="section">
          <div className="section-title">剂量参数 | Dose Matrix</div>

          <div className="form-row">
            <div className="form-group">
              <label className="form-label">加速电压 (kV)</label>
              <input
                type="number" min="0.5" max="100" step="0.5"
                value={params.beamVoltage}
                onChange={e => updateParam('beamVoltage', Math.max(0.5, parseFloat(e.target.value) || 30))}
                className="form-input"
              />
            </div>
            <div className="form-group">
              <label className="form-label">基础停留 (ns)</label>
              <input
                type="number" min="10" max="100000" step="10"
                value={params.baseDwellTime}
                onChange={e => updateParam('baseDwellTime', Math.max(10, parseInt(e.target.value) || 500))}
                className="form-input"
              />
            </div>
          </div>

          <div className="legend" style={{ marginBottom: 10 }}>
            <div className="legend-item">
              <span className="legend-swatch opaque"></span>
              <span>Opaque: ×1.2 / 25nm 刻蚀</span>
            </div>
            <div className="legend-item">
              <span className="legend-swatch clear"></span>
              <span>Clear: ×0.8 / 15nm 刻蚀</span>
            </div>
            <div className="legend-item">
              <span className="legend-swatch" style={{ background: 'rgba(0,212,255,0.3)', borderColor: 'var(--accent-primary)' }}></span>
              <span>Border: ×0.6 / 10nm 刻蚀</span>
            </div>
          </div>

          <button
            className="btn-block btn-secondary"
            onClick={onBuildDoseMatrix}
            disabled={!scanPathResult || processing}
          >
            ⚡ 构建剂量矩阵 / Build Dose Matrix
          </button>
        </div>

        <div className="section">
          <div className="section-title">导出 | Export</div>
          <button
            className="btn-block btn-secondary"
            onClick={onExportSvg}
            disabled={!detectionResult || processing}
          >
            📐 导出 SVG 轮廓
          </button>
          <button
            className="btn-block btn-secondary"
            onClick={onExportCommands}
            disabled={!doseMatrixResult || processing}
          >
            💾 导出修复指令 (.ebr/.csv/.json)
          </button>
        </div>
      </div>
    </aside>
  );
}
