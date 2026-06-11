import React, { useState, useEffect, useRef, useCallback } from 'react';
import { contourToSvgPoints, generateDefectSvg, clamp } from '../utils/svgUtils.js';

export default function ImageViewer(props) {
  const {
    imageData, defects, selectedDefectId, hoveredDefectId,
    onDefectClick, onDefectHover, scanPathResult
  } = props;

  const wrapRef = useRef(null);
  const [viewport, setViewport] = useState({ scale: 1, offsetX: 0, offsetY: 0 });
  const [isDragging, setIsDragging] = useState(false);
  const dragStart = useRef({ x: 0, y: 0, ox: 0, oy: 0 });
  const [mousePos, setMousePos] = useState({ x: 0, y: 0, imgX: 0, imgY: 0 });
  const imgRef = useRef(null);

  useEffect(() => {
    if (wrapRef.current && imageData) {
      const rect = wrapRef.current.getBoundingClientRect();
      const scaleX = rect.width / imageData.width;
      const scaleY = rect.height / imageData.height;
      const fitScale = Math.min(scaleX, scaleY) * 0.95;
      setViewport({
        scale: fitScale,
        offsetX: (rect.width - imageData.width * fitScale) / 2,
        offsetY: (rect.height - imageData.height * fitScale) / 2
      });
    }
  }, [imageData?.width, imageData?.height]);

  const handleWheel = useCallback((e) => {
    e.preventDefault();
    if (!imageData || !wrapRef.current) return;
    const rect = wrapRef.current.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;
    const zoomFactor = e.deltaY > 0 ? 0.9 : 1.1;
    const newScale = clamp(viewport.scale * zoomFactor, 0.05, 40);
    const ratio = newScale / viewport.scale;
    setViewport(v => ({
      scale: newScale,
      offsetX: mx - (mx - v.offsetX) * ratio,
      offsetY: my - (my - v.offsetY) * ratio
    }));
  }, [viewport.scale, imageData, viewport.offsetX, viewport.offsetY]);

  useEffect(() => {
    const el = wrapRef.current;
    if (!el) return;
    el.addEventListener('wheel', handleWheel, { passive: false });
    return () => el.removeEventListener('wheel', handleWheel);
  }, [handleWheel]);

  const handleMouseDown = useCallback((e) => {
    if (e.button !== 0) return;
    setIsDragging(true);
    dragStart.current = {
      x: e.clientX, y: e.clientY,
      ox: viewport.offsetX, oy: viewport.offsetY
    };
  }, [viewport]);

  const handleMouseMove = useCallback((e) => {
    if (!wrapRef.current || !imageData) return;
    const rect = wrapRef.current.getBoundingClientRect();
    const cx = e.clientX - rect.left;
    const cy = e.clientY - rect.top;
    const imgX = (cx - viewport.offsetX) / viewport.scale;
    const imgY = (cy - viewport.offsetY) / viewport.scale;
    setMousePos({ x: cx, y: cy, imgX, imgY });

    if (isDragging) {
      setViewport(v => ({
        ...v,
        offsetX: dragStart.current.ox + (e.clientX - dragStart.current.x),
        offsetY: dragStart.current.oy + (e.clientY - dragStart.current.y)
      }));
    }
  }, [viewport, isDragging, imageData]);

  const handleMouseUp = useCallback(() => {
    setIsDragging(false);
  }, []);

  const handleReset = useCallback(() => {
    if (!wrapRef.current || !imageData) return;
    const rect = wrapRef.current.getBoundingClientRect();
    const scaleX = rect.width / imageData.width;
    const scaleY = rect.height / imageData.height;
    const fitScale = Math.min(scaleX, scaleY) * 0.95;
    setViewport({
      scale: fitScale,
      offsetX: (rect.width - imageData.width * fitScale) / 2,
      offsetY: (rect.height - imageData.height * fitScale) / 2
    });
  }, [imageData]);

  const zoomIn = () => setViewport(v => ({ ...v, scale: clamp(v.scale * 1.25, 0.05, 40) }));
  const zoomOut = () => setViewport(v => ({ ...v, scale: clamp(v.scale / 1.25, 0.05, 40) }));

  const buildScanPathPolylines = () => {
    if (!scanPathResult?.scanLines) return null;
    const lines = [];
    for (let i = 0; i < scanPathResult.scanLines.length; i++) {
      const line = scanPathResult.scanLines[i];
      if (!line.points?.length) continue;
      const pts = line.points.map(p => `${p.x.toFixed(2)},${p.y.toFixed(2)}`).join(' ');
      const color = line.direction === 1 ? '#ffab00' : '#00e5ff';
      lines.push(
        <polyline
          key={i}
          points={pts}
          fill="none"
          stroke={color}
          strokeWidth={Math.max(0.3, 0.8 / viewport.scale)}
          strokeLinecap="round"
          strokeLinejoin="round"
          opacity={0.75}
        />
      );
    }
    return lines;
  };

  const buildScanDoseCircles = () => {
    if (!scanPathResult?.scanLines || viewport.scale < 2) return null;
    const circles = [];
    let idx = 0;
    for (const line of scanPathResult.scanLines) {
      if (!line.points) continue;
      for (const pt of line.points) {
        if (idx++ % 3 !== 0) continue;
        const r = Math.max(0.5, (pt.dose || 1) * 1.2 / viewport.scale);
        const color = pt.material === 'border' ? 'rgba(0,212,255,0.6)' :
          pt.material === 'clear' ? 'rgba(78,205,196,0.5)' : 'rgba(255,171,0,0.5)';
        circles.push(
          <circle
            key={idx}
            cx={pt.x}
            cy={pt.y}
            r={r}
            fill={color}
          />
        );
      }
    }
    return circles;
  };

  const inImgBounds = mousePos.imgX >= 0 && mousePos.imgX < (imageData?.width || 0) &&
    mousePos.imgY >= 0 && mousePos.imgY < (imageData?.height || 0);

  return (
    <>
      <div className="viewer-toolbar" style={{ justifyContent: 'space-between' }}>
        <div className="toolbar-group">
          <button className="tool-btn" onClick={handleReset} title="重置视图 (Fit)">⤢</button>
          <button className="tool-btn" onClick={zoomIn} title="放大 (+)">＋</button>
          <button className="tool-btn" onClick={zoomOut} title="缩小 (-)">－</button>
          <div style={{ padding: '0 8px', fontSize: 11, color: 'var(--text-muted)', fontFamily: 'Consolas,monospace', minWidth: 80, textAlign: 'center' }}>
            {(viewport.scale * 100).toFixed(0)}%
          </div>
        </div>
        <div className="toolbar-group">
          <span style={{ fontSize: 10, color: 'var(--text-muted)' }}>⚙ 拖拽平移 | 滚轮缩放 | 点击选择缺陷</span>
        </div>
      </div>

      <div
        ref={wrapRef}
        className="viewer-canvas-wrap"
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={() => { handleMouseUp(); onDefectHover(null); }}
        style={{ cursor: isDragging ? 'grabbing' : 'crosshair', background: '#06090f' }}
      >
        {!imageData ? (
          <div className="empty-viewer">
            <div className="empty-icon">🔬</div>
            <div className="empty-title">SEM 图像查看器</div>
            <div className="empty-desc">
              点击「加载图像」按钮或使用 Ctrl+O 导入高分辨率电子显微镜掩膜版图像。
              支持 EUV 光刻吸收层的 Opaque/Clear 双类型缺陷检测。
            </div>
          </div>
        ) : (
          <>
            <svg
              width="100%"
              height="100%"
              viewBox={`0 0 ${imageData.width} ${imageData.height}`}
              preserveAspectRatio="xMidYMid meet"
              style={{
                transform: `translate(${viewport.offsetX}px, ${viewport.offsetY}px) scale(${viewport.scale})`,
                transformOrigin: '0 0',
                width: imageData.width,
                height: imageData.height,
                imageRendering: viewport.scale > 2 ? 'pixelated' : 'auto'
              }}
            >
              <defs>
                <pattern id="grid" width="100" height="100" patternUnits="userSpaceOnUse">
                  <path d="M 100 0 L 0 0 0 100" fill="none" stroke="rgba(0,212,255,0.05)" strokeWidth="1" />
                </pattern>
                <filter id="svg-glow">
                  <feGaussianBlur stdDeviation="2" result="coloredBlur" />
                  <feMerge><feMergeNode in="coloredBlur" /><feMergeNode in="SourceGraphic" /></feMerge>
                </filter>
                <filter id="beam-glow">
                  <feGaussianBlur stdDeviation="3" result="b" />
                  <feMerge><feMergeNode in="b" /><feMergeNode in="SourceGraphic" /></feMerge>
                </filter>
              </defs>

              <rect width={imageData.width} height={imageData.height} fill="url(#grid)" />
              
              <image
                ref={imgRef}
                href={imageData.src || imageData.canvas?.toDataURL()}
                width={imageData.width}
                height={imageData.height}
                xlinkHref={imageData.src || imageData.canvas?.toDataURL()}
                preserveAspectRatio="none"
              />

              <g className="scan-path-layer" style={{ pointerEvents: 'none' }}>
                {buildScanDoseCircles()}
                {buildScanPathPolylines()}
              </g>

              <g className="defects-layer">
                {defects.map(defect => {
                  const selected = defect.id === selectedDefectId;
                  const hovered = defect.id === hoveredDefectId;
                  const cfg = generateDefectSvg(defect, selected, hovered);
                  const opacity = (!selectedDefectId || selected || hovered) ? 1 : 0.35;
                  
                  return (
                    <g key={defect.id} style={{ opacity }}>
                      <polygon
                        points={cfg.pointsStr}
                        fill={cfg.fill}
                        stroke={cfg.stroke}
                        strokeWidth={cfg.strokeWidth}
                        strokeDasharray={cfg.dashArray}
                        filter={cfg.glow ? 'url(#svg-glow)' : 'none'}
                        style={{ cursor: 'pointer', transition: 'all 0.15s' }}
                        onClick={(e) => { e.stopPropagation(); onDefectClick(defect.id); }}
                        onMouseEnter={(e) => { e.stopPropagation(); onDefectHover(defect.id); }}
                        onMouseLeave={() => onDefectHover(null)}
                      />
                      {selected && defect.centroid && (
                        <>
                          <circle cx={defect.centroid.x} cy={defect.centroid.y} r={4 / Math.max(0.5, viewport.scale)} fill={cfg.stroke} />
                          <text
                            x={defect.boundingBox?.maxX + 8 || 0}
                            y={defect.boundingBox?.minY || 0}
                            fill={cfg.stroke}
                            fontSize={Math.max(8, 12 / Math.max(0.5, viewport.scale))}
                            fontFamily="Consolas, monospace"
                            style={{ pointerEvents: 'none' }}
                          >
                            {defect.id}
                          </text>
                        </>
                      )}
                    </g>
                  );
                })}
              </g>

              {inImgBounds && (
                <g style={{ pointerEvents: 'none' }}>
                  <line
                    x1={0} y1={mousePos.imgY}
                    x2={imageData.width} y2={mousePos.imgY}
                    stroke="rgba(0,212,255,0.25)"
                    strokeWidth={0.5}
                    strokeDasharray="4 4"
                  />
                  <line
                    x1={mousePos.imgX} y1={0}
                    x2={mousePos.imgX} y2={imageData.height}
                    stroke="rgba(0,212,255,0.25)"
                    strokeWidth={0.5}
                    strokeDasharray="4 4"
                  />
                  <circle
                    cx={mousePos.imgX} cy={mousePos.imgY}
                    r={Math.max(3, 10 / viewport.scale)}
                    fill="none"
                    stroke="var(--accent-primary)"
                    strokeWidth={1}
                  />
                </g>
              )}
            </svg>
          </>
        )}

        {imageData && (
          <div className="viewer-coords">
            {inImgBounds
              ? `IMG: (${mousePos.imgX.toFixed(1)}, ${mousePos.imgY.toFixed(1)}) px  |  ZOOM: ${(viewport.scale * 100).toFixed(0)}%`
              : `SIZE: ${imageData.width} × ${imageData.height} px  |  ZOOM: ${(viewport.scale * 100).toFixed(0)}%`
            }
          </div>
        )}

        {imageData && (
          <div className="viewer-minimap">
            <svg width="100%" height="100%" viewBox={`0 0 ${imageData.width} ${imageData.height}`} preserveAspectRatio="xMidYMid meet">
              <image href={imageData.src || imageData.canvas?.toDataURL()} width={imageData.width} height={imageData.height} opacity="0.6" />
              {defects.map(d => (
                <polygon
                  key={'m' + d.id}
                  points={contourToSvgPoints(d.points)}
                  fill={d.type === 'opaque' ? 'rgba(255,107,107,0.5)' : 'rgba(78,205,196,0.5)'}
                  stroke={d.type === 'opaque' ? '#ff6b6b' : '#4ecdc4'}
                  strokeWidth="0.5"
                />
              ))}
              {wrapRef.current && (() => {
                const rect = wrapRef.current.getBoundingClientRect();
                const viewX = (-viewport.offsetX) / viewport.scale;
                const viewY = (-viewport.offsetY) / viewport.scale;
                const viewW = rect.width / viewport.scale;
                const viewH = rect.height / viewport.scale;
                return (
                  <rect
                    x={viewX} y={viewY} width={viewW} height={viewH}
                    fill="none" stroke="var(--accent-primary)" strokeWidth="1" strokeDasharray="2 1"
                  />
                );
              })()}
            </svg>
          </div>
        )}
      </div>
    </>
  );
}
