import React, { useState, useEffect, useCallback, useRef } from 'react';
import ControlPanel from './components/ControlPanel.jsx';
import ImageViewer from './components/ImageViewer.jsx';
import DefectPanel from './components/DefectPanel.jsx';
import RepairSimulator3D from './components/RepairSimulator3D.jsx';
import ConsoleLog from './components/ConsoleLog.jsx';
import { addLog, formatArea } from './utils/imageUtils.js';
import { getBBoxFromDefects, buildSvgDef, formatDose, formatDwellTime } from './utils/svgUtils.js';

export default function App() {
  const [imageData, setImageData] = useState(null);
  const [layers, setLayers] = useState([]);
  const [currentLayerIdx, setCurrentLayerIdx] = useState(0);
  const [detectionResult, setDetectionResult] = useState(null);
  const [selectedDefectId, setSelectedDefectId] = useState(null);
  const [hoveredDefectId, setHoveredDefectId] = useState(null);
  const [scanPathResult, setScanPathResult] = useState(null);
  const [pecResult, setPecResult] = useState(null);
  const [doseMatrixResult, setDoseMatrixResult] = useState(null);
  const [logs, setLogs] = useState([]);
  const [processing, setProcessing] = useState(null);
  const [progress, setProgress] = useState(0);
  const [nativeLoaded, setNativeLoaded] = useState(false);
  const [currentView, setCurrentView] = useState('2d');
  const [showOpaque, setShowOpaque] = useState(true);
  const [showClear, setShowClear] = useState(true);
  const [showScanPath, setShowScanPath] = useState(true);
  const [pecEnabled, setPecEnabled] = useState(true);
  const [processingParams, setProcessingParams] = useState({
    gaussianSigma: 1.5,
    blockSize: 31,
    C: 10,
    cannyLow: 40,
    cannyHigh: 120,
    stepX: 2,
    stepY: 2,
    overlap: 0.3,
    scanMode: 'hybrid',
    beamVoltage: 30,
    baseDwellTime: 500,
    pec: {
      enabled: true,
      eta: 0.45,
      alpha: 2.5,
      beta: 25.0,
      gamma: 0.9,
      iterations: 5,
      regularizationLambda: 0.001,
      applyDogBoneEnhance: true,
      dogBoneStrength: 1.15,
      maxDoseMultiplier: 2.5,
      minDoseMultiplier: 0.5,
      useWienerFilter: false,
      wienerSNR: 30.0
    }
  });

  const appendLog = useCallback((msg, type = 'info') => {
    setLogs(prev => addLog(prev, msg, type));
  }, []);

  useEffect(() => {
    const loaded = window.ebeamNative?.isLoaded?.() || false;
    setNativeLoaded(loaded);
    const ver = window.ebeamNative?.getVersion?.() || 'N/A';
    appendLog(`系统启动 - 原生模块: ${loaded ? '已加载 ✓' : 'JS 回退模式'}, 版本: ${ver}`, loaded ? 'success' : 'warn');
    appendLog('E-Beam Reticle Repair System v1.0.0 初始化完成', 'success');
    appendLog(`当前平台: ${navigator.platform}`, 'info');

    const cleanupLoadImage = window.electronAPI?.onLoadImage?.(() => handleLoadImage());
    const cleanupLoadMulti = window.electronAPI?.onLoadMultiLayer?.(() => handleLoadMultiLayer());
    const cleanupRunDet = window.electronAPI?.onRunDetection?.(() => handleRunDetection());
    const cleanupGenScan = window.electronAPI?.onGenerateScanPath?.(() => handleGenerateScanPath());
    const cleanupRunSim = window.electronAPI?.onRunSimulation?.(() => handleRunSimulation());
    const cleanupExpCmd = window.electronAPI?.onExportCommands?.(() => handleExportCommands());
    const cleanupExpSvg = window.electronAPI?.onExportSvg?.(() => handleExportSvg());

    return () => {
      cleanupLoadImage?.();
      cleanupLoadMulti?.();
      cleanupRunDet?.();
      cleanupGenScan?.();
      cleanupRunSim?.();
      cleanupExpCmd?.();
      cleanupExpSvg?.();
    };
  }, []);

  const loadImageFromPath = useCallback(async (filePath) => {
    try {
      const res = await window.electronAPI.readFile(filePath);
      if (!res.success) throw new Error(res.error);
      
      const dataUrl = `data:image;base64,${res.data}`;
      const img = new Image();
      await new Promise((resolve, reject) => {
        img.onload = resolve;
        img.onerror = reject;
        img.src = dataUrl;
      });

      const canvas = document.createElement('canvas');
      canvas.width = img.naturalWidth;
      canvas.height = img.naturalHeight;
      const ctx = canvas.getContext('2d');
      ctx.drawImage(img, 0, 0);
      const imgData = ctx.getImageData(0, 0, canvas.width, canvas.height);

      const result = {
        width: canvas.width,
        height: canvas.height,
        channels: 4,
        data: new Uint8Array(imgData.data),
        path: filePath,
        name: filePath.split(/[\\/]/).pop(),
        src: dataUrl,
        canvas
      };

      return result;
    } catch (err) {
      appendLog(`加载图像失败: ${err.message}`, 'error');
      return null;
    }
  }, [appendLog]);

  const handleLoadImage = useCallback(async () => {
    try {
      const result = await window.electronAPI.openFile({
        title: '加载 SEM 掩膜版图像',
        filters: [
          { name: 'SEM 图像', extensions: ['tif', 'tiff', 'png', 'bmp', 'jpg', 'jpeg', 'raw'] },
          { name: '所有文件', extensions: ['*'] }
        ]
      });
      if (result.canceled || !result.filePaths?.length) return;
      
      const filePath = result.filePaths[0];
      setProcessing('加载图像');
      setProgress(20);
      
      const img = await loadImageFromPath(filePath);
      if (!img) {
        setProcessing(null);
        return;
      }
      
      setProgress(80);
      setImageData(img);
      setLayers([img]);
      setCurrentLayerIdx(0);
      setDetectionResult(null);
      setScanPathResult(null);
      setDoseMatrixResult(null);
      setSelectedDefectId(null);
      setProgress(100);
      setTimeout(() => { setProcessing(null); setProgress(0); }, 300);
      appendLog(`图像已加载: ${img.name} (${img.width}×${img.height})`, 'success');
    } catch (err) {
      appendLog(`加载图像异常: ${err.message}`, 'error');
      setProcessing(null);
    }
  }, [loadImageFromPath, appendLog]);

  const handleLoadMultiLayer = useCallback(async () => {
    try {
      const result = await window.electronAPI.openFiles({
        title: '加载多层 SEM 图像'
      });
      if (result.canceled || !result.filePaths?.length) return;
      
      setProcessing('加载多层图像');
      const loaded = [];
      
      for (let i = 0; i < result.filePaths.length; i++) {
        setProgress(Math.round((i / result.filePaths.length) * 80));
        const img = await loadImageFromPath(result.filePaths[i]);
        if (img) {
          img.name = img.name || `Layer_${i}`;
          loaded.push(img);
        }
      }
      
      if (loaded.length) {
        setLayers(loaded);
        setCurrentLayerIdx(0);
        setImageData(loaded[0]);
        setDetectionResult(null);
        setScanPathResult(null);
        setDoseMatrixResult(null);
        setProgress(100);
        setTimeout(() => { setProcessing(null); setProgress(0); }, 300);
        appendLog(`已加载 ${loaded.length} 层图像`, 'success');
      } else {
        setProcessing(null);
      }
    } catch (err) {
      appendLog(`加载多层图像异常: ${err.message}`, 'error');
      setProcessing(null);
    }
  }, [loadImageFromPath, appendLog]);

  const handleRunDetection = useCallback(async () => {
    if (!imageData) {
      appendLog('请先加载图像', 'warn');
      return;
    }
    
    try {
      setProcessing('缺陷检测');
      setProgress(10);
      appendLog('开始缺陷检测分析...', 'info');
      appendLog(`参数: σ=${processingParams.gaussianSigma}, block=${processingParams.blockSize}, C=${processingParams.C}, Canny=[${processingParams.cannyLow},${processingParams.cannyHigh}]`, 'info');
      
      await new Promise(r => setTimeout(r, 200));
      setProgress(25);
      
      const res = await window.ebeamNative.detectDefects(imageData, processingParams);
      setProgress(75);
      
      if (res.success) {
        setDetectionResult(res);
        const total = res.opaqueDefects.length + res.clearDefects.length;
        const totalArea = [...res.opaqueDefects, ...res.clearDefects].reduce((s, d) => s + d.area, 0);
        appendLog(`检测完成: ${res.opaqueDefects.length} Opaque + ${res.clearDefects.length} Clear = ${total} 个缺陷`, 'success');
        appendLog(`处理耗时: ${res.meta?.processingTimeMs?.toFixed(1) || 'N/A'} ms, 总面积: ${formatArea(totalArea)}`, 'info');
        if (!res.native) appendLog('提示: 使用 JS 回退算法 (可编译 C++ 原生模块提升性能)', 'warn');
      } else {
        appendLog(`检测失败: ${res.error || '未知错误'}`, 'error');
      }
      
      setProgress(100);
      setTimeout(() => { setProcessing(null); setProgress(0); }, 400);
    } catch (err) {
      appendLog(`检测异常: ${err.message}`, 'error');
      setProcessing(null);
    }
  }, [imageData, processingParams, appendLog]);

  const handleGenerateScanPath = useCallback(async () => {
    if (!detectionResult) {
      appendLog('请先运行缺陷检测', 'warn');
      return;
    }

    const selected = getSelectedDefect();
    const targets = selected ? [selected] : [...detectionResult.opaqueDefects, ...detectionResult.clearDefects];
    
    if (!targets.length) {
      appendLog('没有可处理的缺陷', 'warn');
      return;
    }

    try {
      setProcessing('生成扫描路径');
      setProgress(10);
      appendLog(`开始生成扫描路径: ${targets.length} 个目标, 模式=${processingParams.scanMode}, step=[${processingParams.stepX},${processingParams.stepY}]`, 'info');
      
      const allLines = [];
      let totalPts = 0;
      
      for (let i = 0; i < targets.length; i++) {
        setProgress(10 + Math.round((i / targets.length) * 60));
        const defect = targets[i];
        const opts = {
          ...processingParams,
          layer: defect.layer || 0,
          mode: processingParams.scanMode
        };
        
        const res = await window.ebeamNative.generateRasterScan(defect.points, opts);
        if (res.success && res.scanLines?.length) {
          for (const line of res.scanLines) {
            for (const pt of line.points) {
              pt.defectId = defect.id;
              pt.defectType = defect.type;
            }
            allLines.push(line);
          }
          totalPts += res.totalPoints;
        }
      }
      
      setProgress(80);
      const combined = {
        success: true,
        native: allLines.length > 0,
        scanLines: allLines,
        totalPoints: totalPts,
        bbox: getBBoxFromDefects(targets)
      };
      
      setScanPathResult(combined);
      appendLog(`扫描路径生成完成: ${allLines.length} 行, ${totalPts} 曝光点`, 'success');
      setProgress(100);
      setTimeout(() => { setProcessing(null); setProgress(0); }, 400);
    } catch (err) {
      appendLog(`扫描路径异常: ${err.message}`, 'error');
      setProcessing(null);
    }
  }, [detectionResult, selectedDefectId, processingParams, appendLog]);

  const handleBuildDoseMatrix = useCallback(async () => {
    if (!scanPathResult) {
      appendLog('请先生成扫描路径', 'warn');
      return;
    }

    try {
      setProcessing('构建剂量矩阵');
      setProgress(15);
      appendLog(`开始剂量矩阵: 电压=${processingParams.beamVoltage}kV, 基础停留=${processingParams.baseDwellTime}ns`, 'info');
      
      await new Promise(r => setTimeout(r, 200));
      setProgress(45);
      
      const opts = {
        beamVoltage: processingParams.beamVoltage,
        baseDwellTime: processingParams.baseDwellTime,
        calibration: {
          opaque: { multiplier: 1.2, etchDepth: 25 },
          clear: { multiplier: 0.8, etchDepth: 15 },
          border: { multiplier: 0.6, etchDepth: 10 },
          target: { multiplier: 1.0, etchDepth: 20 }
        }
      };
      
      const res = await window.ebeamNative.buildDoseMatrix(scanPathResult.scanLines, opts);
      setProgress(85);
      
      if (res.success) {
        setDoseMatrixResult(res);
        appendLog(`剂量矩阵完成: ${res.stats.totalCommands} 条指令, 总剂量: ${formatDose(res.stats.totalDose)}`, 'success');
        appendLog(`最大停留: ${formatDwellTime(res.stats.maxDwellTime)}, 平均: ${formatDwellTime(res.stats.avgDwellTime)}`, 'info');
      } else {
        appendLog(`剂量矩阵失败: ${res.error || '未知'}`, 'error');
      }
      
      setProgress(100);
      setTimeout(() => { setProcessing(null); setProgress(0); }, 400);
    } catch (err) {
      appendLog(`剂量矩阵异常: ${err.message}`, 'error');
      setProcessing(null);
    }
  }, [scanPathResult, processingParams, appendLog]);

  const handleApplyPEC = useCallback(async () => {
    if (!scanPathResult) {
      appendLog('请先生成扫描路径', 'warn');
      return;
    }
    const pecCfg = processingParams.pec || {};
    if (!pecCfg.enabled && !processingParams._forcePEC) {
      appendLog('PEC 校正未启用', 'warn');
    }

    const w = imageData?.width || (detectionResult?.meta?.width) || 2048;
    const h = imageData?.height || (detectionResult?.meta?.height) || 2048;

    try {
      setProcessing('PEC邻近效应校正');
      setProgress(5);
      appendLog(
        `启动 PEC 频域校正: α=${pecCfg.alpha?.toFixed(1)}nm β=${pecCfg.beta?.toFixed(1)}nm η=${pecCfg.eta?.toFixed(2)} ` +
        `${pecCfg.applyDogBoneEnhance ? 'Dog-bone ' + pecCfg.dogBoneStrength?.toFixed(2) + '× 增强' : ''}`,
        'info'
      );

      const psfParams = {
        eta: pecCfg.eta ?? 0.45,
        alpha: pecCfg.alpha ?? 2.5,
        beta: pecCfg.beta ?? 25.0,
        gamma: pecCfg.gamma ?? 0.9
      };
      const pecOpts = {
        iterations: pecCfg.iterations ?? 5,
        regularizationLambda: pecCfg.regularizationLambda ?? 0.001,
        applyDogBoneEnhance: pecCfg.applyDogBoneEnhance ?? true,
        dogBoneStrength: pecCfg.dogBoneStrength ?? 1.15,
        maxDoseMultiplier: pecCfg.maxDoseMultiplier ?? 2.5,
        minDoseMultiplier: pecCfg.minDoseMultiplier ?? 0.5,
        useWienerFilter: pecCfg.useWienerFilter ?? false,
        wienerSNR: pecCfg.wienerSNR ?? 30.0
      };

      setProgress(25);
      const res = await window.ebeamNative.applyPECCorrection(
        scanPathResult.scanLines, w, h, psfParams, pecOpts
      );
      setProgress(85);

      if (res.success) {
        setPecResult(res);
        setScanPathResult(prev => prev ? {
          ...prev,
          scanLines: res.correctedScanLines,
          pecApplied: true,
          pecStats: res.stats,
          pecPSF: res.psf,
          pecAlgorithm: res.algorithm
        } : prev);

        const s = res.stats || {};
        appendLog(
          `PEC 校正完成: ${s.iterations} 次迭代, 耗时 ${(s.processingTimeMs || 0).toFixed(1)}ms`,
          'success'
        );
        appendLog(
          `校正系数: 最小 ${(s.minCorrectionFactor || 1).toFixed(3)}×, ` +
          `最大 ${(s.maxCorrectionFactor || 1).toFixed(3)}×, ` +
          `平均 ${(s.avgCorrectionFactor || 1).toFixed(3)}×`,
          'info'
        );
        if (res.algorithm?.applyDogBoneEnhance) {
          appendLog(`Dog-bone 异形增强强度: ${(res.algorithm.dogBoneStrength || 1.15).toFixed(2)}×`, 'info');
        }
      } else {
        appendLog(`PEC 校正失败: ${res.error || '未知'}`, 'error');
      }

      setProgress(100);
      setTimeout(() => { setProcessing(null); setProgress(0); }, 400);
    } catch (err) {
      appendLog(`PEC 校正异常: ${err.message}`, 'error');
      setProcessing(null);
    }
  }, [scanPathResult, processingParams, imageData, detectionResult, appendLog]);

  const handleRunSimulation = useCallback(async () => {
    if (!detectionResult) {
      appendLog('请先完成缺陷检测', 'warn');
      return;
    }
    
    if (!scanPathResult) {
      await handleGenerateScanPath();
    }
    if (!doseMatrixResult) {
      await handleBuildDoseMatrix();
    }
    
    setCurrentView('3d');
    appendLog('切换到 3D 修复模拟视图', 'info');
  }, [detectionResult, scanPathResult, doseMatrixResult, handleGenerateScanPath, handleBuildDoseMatrix, appendLog]);

  const handleExportCommands = useCallback(async () => {
    if (!doseMatrixResult) {
      appendLog('请先构建剂量矩阵', 'warn');
      return;
    }

    try {
      const res = await window.electronAPI.saveFile({
        title: '导出修复指令',
        defaultPath: `repair_commands_${Date.now()}.ebr`,
        filters: [
          { name: 'E-Beam 指令文件', extensions: ['ebr', 'bin'] },
          { name: 'CSV 文件', extensions: ['csv'] },
          { name: 'JSON 文件', extensions: ['json'] }
        ]
      });
      if (res.canceled || !res.filePath) return;
      
      setProcessing('导出指令');
      setProgress(40);
      
      const ext = res.filePath.split('.').pop().toLowerCase();
      let data, encoding = 'utf8';
      
      if (ext === 'json') {
        data = doseMatrixResult;
        encoding = 'json';
      } else if (ext === 'csv') {
        let csv = 'X,Y,Z,XVoltage,YVoltage,ZVoltage,DwellTime_ns,Dose_nC,Material,Layer\n';
        for (const c of doseMatrixResult.commands) {
          csv += `${c.x},${c.y},${c.z},${c.xVoltage},${c.yVoltage},${c.zVoltage},${c.dwellTime},${c.dose},${c.material},${c.layer}\n`;
        }
        data = csv;
      } else {
        data = btoa(String.fromCharCode(...new Uint8Array(
          doseMatrixResult.commands.flatMap(c => [
            ...new Float32Array([c.x, c.y, c.z, c.xVoltage, c.yVoltage, c.zVoltage]).buffer,
            ...new Int32Array([c.dwellTime, c.layer]).buffer
          ])
        )));
        encoding = 'base64';
      }
      
      setProgress(75);
      const writeRes = await window.electronAPI.writeFile(res.filePath, data, encoding);
      setProgress(100);
      
      if (writeRes.success) {
        appendLog(`指令导出成功: ${writeRes.path}`, 'success');
      } else {
        appendLog(`导出失败: ${writeRes.error}`, 'error');
      }
      
      setTimeout(() => { setProcessing(null); setProgress(0); }, 300);
    } catch (err) {
      appendLog(`导出异常: ${err.message}`, 'error');
      setProcessing(null);
    }
  }, [doseMatrixResult, appendLog]);

  const handleExportSvg = useCallback(async () => {
    if (!detectionResult) {
      appendLog('请先完成缺陷检测', 'warn');
      return;
    }
    try {
      const res = await window.electronAPI.saveFile({
        title: '导出 SVG 轮廓',
        defaultPath: `defects_${Date.now()}.svg`,
        filters: [{ name: 'SVG 矢量图', extensions: ['svg'] }]
      });
      if (res.canceled || !res.filePath) return;
      
      const allDefects = [...detectionResult.opaqueDefects, ...detectionResult.clearDefects];
      const bbox = imageData ? {
        minX: 0, minY: 0, maxX: imageData.width, maxY: imageData.height,
        width: imageData.width, height: imageData.height
      } : getBBoxFromDefects(allDefects);
      const svg = buildSvgDef(allDefects, bbox);
      
      const writeRes = await window.electronAPI.writeFile(res.filePath, svg, 'utf8');
      if (writeRes.success) appendLog(`SVG 导出成功: ${writeRes.path}`, 'success');
      else appendLog(`导出失败: ${writeRes.error}`, 'error');
    } catch (err) {
      appendLog(`导出异常: ${err.message}`, 'error');
    }
  }, [detectionResult, imageData, appendLog]);

  const getSelectedDefect = useCallback(() => {
    if (!detectionResult || !selectedDefectId) return null;
    return [...detectionResult.opaqueDefects, ...detectionResult.clearDefects].find(d => d.id === selectedDefectId) || null;
  }, [detectionResult, selectedDefectId]);

  const handleDefectClick = useCallback((id) => {
    setSelectedDefectId(prev => prev === id ? null : id);
  }, []);

  const handleLayerChange = useCallback((idx) => {
    if (layers[idx]) {
      setCurrentLayerIdx(idx);
      setImageData(layers[idx]);
      setDetectionResult(null);
      setScanPathResult(null);
      setDoseMatrixResult(null);
      appendLog(`切换到图层 ${idx + 1}: ${layers[idx].name || `Layer_${idx}`}`, 'info');
    }
  }, [layers, appendLog]);

  const allDefects = detectionResult ? [
    ...(showOpaque ? detectionResult.opaqueDefects : []),
    ...(showClear ? detectionResult.clearDefects : [])
  ] : [];

  return (
    <div className="app">
      <header className="app-header">
        <div className="app-logo">
          <div className="app-logo-icon">E</div>
          <div>
            <div style={{ fontWeight: 700 }}>E-Beam Reticle Repair</div>
            <div style={{ fontSize: 10, color: 'var(--text-muted)', letterSpacing: 1 }}>SEMICONDUCTOR EUV MASK DEFECT MANAGEMENT</div>
          </div>
        </div>
        
        <div className="app-header-center">
          <button className="header-btn primary" onClick={handleLoadImage}>
            <span>📂</span> 加载图像
          </button>
          <button className="header-btn" onClick={handleLoadMultiLayer}>
            <span>🗂️</span> 多层
          </button>
          <button className="header-btn" onClick={handleRunDetection} disabled={!imageData || processing}>
            <span>🔍</span> 缺陷检测
          </button>
          <button className="header-btn" onClick={handleGenerateScanPath} disabled={!detectionResult || processing}>
            <span>🛣️</span> 扫描路径
          </button>
          <button
            className="header-btn"
            style={{ background: processingParams?.pec?.enabled ? 'rgba(120, 180, 255, 0.15)' : 'transparent' }}
            onClick={handleApplyPEC}
            disabled={!scanPathResult || processing}
          >
            <span>🧠</span> PEC校正
          </button>
          <button className="header-btn" onClick={handleBuildDoseMatrix} disabled={!scanPathResult || processing}>
            <span>⚡</span> 剂量矩阵
          </button>
          <button className="header-btn" onClick={handleRunSimulation} disabled={!detectionResult || processing}>
            <span>🧪</span> 模拟修复
          </button>
        </div>
        
        <div className="app-header-right">
          <div className="status-indicator">
            <div className={`status-dot ${nativeLoaded ? '' : 'warning'}`}></div>
            <span>{nativeLoaded ? 'Native Ready' : 'JS Fallback'}</span>
          </div>
          <div className="status-indicator">
            <div className={`status-dot ${imageData ? '' : 'warning'}`}></div>
            <span>{imageData ? 'Image Loaded' : 'No Image'}</span>
          </div>
          <div className="status-indicator">
            <div className={`status-dot ${processing ? 'warning' : ''}`} style={{ background: processing ? 'var(--warning)' : 'var(--text-muted)' }}></div>
            <span>{processing || 'Idle'}</span>
          </div>
        </div>
      </header>

      <main className="app-main">
        <ControlPanel
          params={processingParams}
          setParams={setProcessingParams}
          onLoadImage={handleLoadImage}
          onRunDetection={handleRunDetection}
          onGenerateScanPath={handleGenerateScanPath}
          onApplyPEC={handleApplyPEC}
          onBuildDoseMatrix={handleBuildDoseMatrix}
          onExportCommands={handleExportCommands}
          onExportSvg={handleExportSvg}
          processing={processing}
          progress={progress}
          imageData={imageData}
          detectionResult={detectionResult}
          scanPathResult={scanPathResult}
          pecResult={pecResult}
          doseMatrixResult={doseMatrixResult}
          layers={layers}
          currentLayerIdx={currentLayerIdx}
          onLayerChange={handleLayerChange}
          pecEnabled={pecEnabled}
          setPecEnabled={setPecEnabled}
        />

        <div className="center-viewer">
          {currentView === '2d' ? (
            <ImageViewer
              imageData={imageData}
              defects={allDefects}
              selectedDefectId={selectedDefectId}
              hoveredDefectId={hoveredDefectId}
              onDefectClick={handleDefectClick}
              onDefectHover={setHoveredDefectId}
              scanPathResult={showScanPath ? scanPathResult : null}
              showOpaque={showOpaque}
              showClear={showClear}
            />
          ) : (
            <RepairSimulator3D
              imageData={imageData}
              defects={allDefects}
              selectedDefectId={selectedDefectId}
              scanPathResult={scanPathResult}
              doseMatrixResult={doseMatrixResult}
            />
          )}
          
          <div className="viewer-toolbar" style={{ borderTop: '1px solid var(--border-color)', borderBottom: 'none', borderTopWidth: '1px' }}>
            <div className="toolbar-group">
              <button
                className={`tool-btn ${currentView === '2d' ? 'active' : ''}`}
                onClick={() => setCurrentView('2d')}
                title="2D 视图"
              >
                🖼️
              </button>
              <button
                className={`tool-btn ${currentView === '3d' ? 'active' : ''}`}
                onClick={() => setCurrentView('3d')}
                title="3D 修复模拟"
              >
                🧊
              </button>
            </div>
            <div className="toolbar-group">
              <button
                className={`tool-btn ${showOpaque ? 'active' : ''}`}
                onClick={() => setShowOpaque(v => !v)}
                title="显示 Opaque 缺陷"
                style={{ color: showOpaque ? 'var(--opaque)' : '' }}
              >
                🔴
              </button>
              <button
                className={`tool-btn ${showClear ? 'active' : ''}`}
                onClick={() => setShowClear(v => !v)}
                title="显示 Clear 缺陷"
                style={{ color: showClear ? 'var(--clear)' : '' }}
              >
                🟢
              </button>
              <button
                className={`tool-btn ${showScanPath ? 'active' : ''}`}
                onClick={() => setShowScanPath(v => !v)}
                title="显示扫描路径"
              >
                ⛶
              </button>
            </div>
            <div className="toolbar-group">
              <div style={{ padding: '0 8px', fontSize: 11, color: 'var(--text-muted)', fontFamily: 'Consolas,monospace' }}>
                {imageData ? `${imageData.width} × ${imageData.height}` : '--'}
                {detectionResult ? ` · ${allDefects.length} defects` : ''}
                {scanPathResult ? ` · ${scanPathResult.totalPoints} pts` : ''}
              </div>
            </div>
          </div>
        </div>

        <DefectPanel
          detectionResult={detectionResult}
          selectedDefectId={selectedDefectId}
          hoveredDefectId={hoveredDefectId}
          onDefectClick={handleDefectClick}
          onDefectHover={setHoveredDefectId}
          scanPathResult={scanPathResult}
          doseMatrixResult={doseMatrixResult}
        />
      </main>

      <ConsoleLog logs={logs} />
    </div>
  );
}
