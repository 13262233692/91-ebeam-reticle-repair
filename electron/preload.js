const { contextBridge, ipcRenderer } = require('electron');
const path = require('path');

let nativeModule = null;

function loadNativeModule() {
  if (nativeModule) return nativeModule;
  try {
    const bindings = require('bindings');
    nativeModule = bindings('ebeam-repair');
    console.log('[Preload] Native module loaded successfully');
  } catch (err) {
    console.warn('[Preload] Failed to load native module:', err.message);
    console.warn('[Preload] Falling back to JavaScript polyfill');
    nativeModule = null;
  }
  return nativeModule;
}

contextBridge.exposeInMainWorld('electronAPI', {
  // ========== Dialog APIs ==========
  openFile: (options) => ipcRenderer.invoke('dialog:open-file', options),
  openFiles: (options) => ipcRenderer.invoke('dialog:open-files', options),
  saveFile: (options) => ipcRenderer.invoke('dialog:save-file', options),

  // ========== Filesystem APIs ==========
  readFile: (filePath) => ipcRenderer.invoke('fs:read-file', filePath),
  readImageBuffer: (filePath) => ipcRenderer.invoke('fs:read-image-buffer', filePath),
  writeFile: (filePath, data, encoding) => ipcRenderer.invoke('fs:write-file', filePath, data, encoding),

  // ========== System APIs ==========
  getSystemInfo: () => ipcRenderer.invoke('system:get-info'),

  // ========== Logging APIs ==========
  logInfo: (...args) => ipcRenderer.send('log:info', ...args),
  logError: (...args) => ipcRenderer.send('log:error', ...args),
  logWarn: (...args) => ipcRenderer.send('log:warn', ...args),

  // ========== Menu Event Listeners ==========
  onLoadImage: (callback) => {
    ipcRenderer.on('menu:load-image', () => callback());
    return () => ipcRenderer.removeListener('menu:load-image', callback);
  },
  onLoadMultiLayer: (callback) => {
    ipcRenderer.on('menu:load-multi-layer', () => callback());
    return () => ipcRenderer.removeListener('menu:load-multi-layer', callback);
  },
  onExportCommands: (callback) => {
    ipcRenderer.on('menu:export-commands', () => callback());
    return () => ipcRenderer.removeListener('menu:export-commands', callback);
  },
  onExportSvg: (callback) => {
    ipcRenderer.on('menu:export-svg', () => callback());
    return () => ipcRenderer.removeListener('menu:export-svg', callback);
  },
  onRunDetection: (callback) => {
    ipcRenderer.on('menu:run-detection', () => callback());
    return () => ipcRenderer.removeListener('menu:run-detection', callback);
  },
  onGenerateScanPath: (callback) => {
    ipcRenderer.on('menu:generate-scan-path', () => callback());
    return () => ipcRenderer.removeListener('menu:generate-scan-path', callback);
  },
  onRunSimulation: (callback) => {
    ipcRenderer.on('menu:run-simulation', () => callback());
    return () => ipcRenderer.removeListener('menu:run-simulation', callback);
  }
});

contextBridge.exposeInMainWorld('ebeamNative', {
  isLoaded: () => {
    return loadNativeModule() !== null;
  },

  getVersion: () => {
    const mod = loadNativeModule();
    if (mod && mod.getVersion) return mod.getVersion();
    return 'js-fallback-1.0.0';
  },

  detectDefects: async (imageData, options = {}) => {
    const mod = loadNativeModule();
    if (mod && mod.detectDefects) {
      return mod.detectDefects(imageData, options);
    }
    return fallbackDetectDefects(imageData, options);
  },

  generateRasterScan: async (polygonPoints, options = {}) => {
    const mod = loadNativeModule();
    if (mod && mod.generateRasterScan) {
      return mod.generateRasterScan(polygonPoints, options);
    }
    return fallbackGenerateRasterScan(polygonPoints, options);
  },

  buildDoseMatrix: async (scanPoints, materialMap = {}, options = {}) => {
    const mod = loadNativeModule();
    if (mod && mod.buildDoseMatrix) {
      return mod.buildDoseMatrix(scanPoints, materialMap, options);
    }
    return fallbackBuildDoseMatrix(scanPoints, materialMap, options);
  },

  processMultiLayer: async (layers, options = {}) => {
    const mod = loadNativeModule();
    if (mod && mod.processMultiLayer) {
      return mod.processMultiLayer(layers, options);
    }
    return fallbackProcessMultiLayer(layers, options);
  },

  subpixelRefineContour: async (contour, imageData, options = {}) => {
    const mod = loadNativeModule();
    if (mod && mod.subpixelRefineContour) {
      return mod.subpixelRefineContour(contour, imageData, options);
    }
    return fallbackSubpixelRefine(contour, imageData, options);
  }
});

// ==================== JavaScript Fallbacks ====================

function fallbackDetectDefects(imageData, options) {
  console.log('[Fallback] Running JS defect detection...');
  const { width, height } = imageData;
  const pixels = imageData.data;
  
  const gray = new Float32Array(width * height);
  for (let i = 0; i < width * height; i++) {
    const idx = i * 4;
    gray[i] = 0.299 * pixels[idx] + 0.587 * pixels[idx + 1] + 0.114 * pixels[idx + 2];
  }

  const gaussian = gaussianBlur(gray, width, height, options.gaussianSigma || 1.5);
  const edges = sobelEdge(gaussian, width, height);
  const binary = adaptiveThreshold(gaussian, width, height, options.blockSize || 31, options.C || 10);
  
  const { opaqueContours, clearContours } = extractContours(binary, edges, width, height);

  const refinedOpaque = opaqueContours.map(c => subpixelRefineJS(c, gaussian, width, height, 'opaque'));
  const refinedClear = clearContours.map(c => subpixelRefineJS(c, gaussian, width, height, 'clear'));

  return {
    success: true,
    native: false,
    opaqueDefects: refinedOpaque.map((c, i) => ({
      id: `opaque_${i}`,
      type: 'opaque',
      points: c,
      area: polygonArea(c),
      centroid: polygonCentroid(c),
      boundingBox: polygonBBox(c)
    })),
    clearDefects: refinedClear.map((c, i) => ({
      id: `clear_${i}`,
      type: 'clear',
      points: c,
      area: polygonArea(c),
      centroid: polygonCentroid(c),
      boundingBox: polygonBBox(c)
    })),
    meta: {
      width,
      height,
      processingTimeMs: 0
    }
  };
}

function gaussianBlur(src, w, h, sigma) {
  const ksize = Math.max(3, Math.ceil(sigma * 3) * 2 + 1);
  const half = Math.floor(ksize / 2);
  const kernel = new Float32Array(ksize * ksize);
  let sum = 0;
  const twoSigma2 = 2 * sigma * sigma;
  
  for (let y = -half; y <= half; y++) {
    for (let x = -half; x <= half; x++) {
      const val = Math.exp(-(x * x + y * y) / twoSigma2);
      kernel[(y + half) * ksize + (x + half)] = val;
      sum += val;
    }
  }
  for (let i = 0; i < kernel.length; i++) kernel[i] /= sum;

  const dst = new Float32Array(w * h);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let s = 0;
      for (let ky = -half; ky <= half; ky++) {
        for (let kx = -half; kx <= half; kx++) {
          const px = Math.min(w - 1, Math.max(0, x + kx));
          const py = Math.min(h - 1, Math.max(0, y + ky));
          s += src[py * w + px] * kernel[(ky + half) * ksize + (kx + half)];
        }
      }
      dst[y * w + x] = s;
    }
  }
  return dst;
}

function sobelEdge(src, w, h) {
  const gx = new Float32Array(w * h);
  const gy = new Float32Array(w * h);
  const mag = new Float32Array(w * h);

  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      const tl = src[(y - 1) * w + (x - 1)];
      const tc = src[(y - 1) * w + x];
      const tr = src[(y - 1) * w + (x + 1)];
      const ml = src[y * w + (x - 1)];
      const mr = src[y * w + (x + 1)];
      const bl = src[(y + 1) * w + (x - 1)];
      const bc = src[(y + 1) * w + x];
      const br = src[(y + 1) * w + (x + 1)];

      const sx = -tl - 2 * ml - bl + tr + 2 * mr + br;
      const sy = -tl - 2 * tc - tr + bl + 2 * bc + br;

      gx[y * w + x] = sx;
      gy[y * w + x] = sy;
      mag[y * w + x] = Math.sqrt(sx * sx + sy * sy);
    }
  }

  let maxMag = 0;
  for (let i = 0; i < mag.length; i++) if (mag[i] > maxMag) maxMag = mag[i];
  for (let i = 0; i < mag.length; i++) mag[i] = maxMag > 0 ? mag[i] / maxMag : 0;

  const nms = new Float32Array(w * h);
  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      const idx = y * w + x;
      const angle = Math.atan2(gy[idx], gx[idx]);
      const m = mag[idx];
      
      let a, b;
      const deg = angle * 180 / Math.PI;
      const absDeg = Math.abs(deg);
      
      if ((absDeg <= 22.5) || (absDeg > 157.5 && absDeg <= 180)) {
        a = mag[idx - 1]; b = mag[idx + 1];
      } else if ((absDeg > 22.5 && absDeg <= 67.5)) {
        a = mag[(y - 1) * w + (x + 1)]; b = mag[(y + 1) * w + (x - 1)];
      } else if ((absDeg > 67.5 && absDeg <= 112.5)) {
        a = mag[(y - 1) * w + x]; b = mag[(y + 1) * w + x];
      } else {
        a = mag[(y - 1) * w + (x - 1)]; b = mag[(y + 1) * w + (x + 1)];
      }
      
      nms[idx] = (m >= a && m >= b) ? m : 0;
    }
  }

  const high = 0.15;
  const low = 0.07;
  const result = new Uint8Array(w * h);
  const strong = 255;
  const weak = 128;

  for (let i = 0; i < nms.length; i++) {
    if (nms[i] >= high) result[i] = strong;
    else if (nms[i] >= low) result[i] = weak;
  }

  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      const idx = y * w + x;
      if (result[idx] === weak) {
        const tl = result[(y - 1) * w + (x - 1)] === strong;
        const tc = result[(y - 1) * w + x] === strong;
        const tr = result[(y - 1) * w + (x + 1)] === strong;
        const ml = result[y * w + (x - 1)] === strong;
        const mr = result[y * w + (x + 1)] === strong;
        const bl = result[(y + 1) * w + (x - 1)] === strong;
        const bc = result[(y + 1) * w + x] === strong;
        const br = result[(y + 1) * w + (x + 1)] === strong;
        result[idx] = (tl || tc || tr || ml || mr || bl || bc || br) ? strong : 0;
      }
    }
  }
  return result;
}

function adaptiveThreshold(src, w, h, blockSize, C) {
  const half = Math.floor(blockSize / 2);
  const result = new Uint8Array(w * h);
  const intImg = new Float32Array((w + 1) * (h + 1));

  for (let y = 1; y <= h; y++) {
    let rowSum = 0;
    for (let x = 1; x <= w; x++) {
      rowSum += src[(y - 1) * w + (x - 1)];
      intImg[y * (w + 1) + x] = intImg[(y - 1) * (w + 1) + x] + rowSum;
    }
  }

  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const x1 = Math.max(0, x - half);
      const y1 = Math.max(0, y - half);
      const x2 = Math.min(w - 1, x + half);
      const y2 = Math.min(h - 1, y + half);
      const count = (x2 - x1 + 1) * (y2 - y1 + 1);
      
      const sum = intImg[(y2 + 1) * (w + 1) + (x2 + 1)]
                - intImg[y1 * (w + 1) + (x2 + 1)]
                - intImg[(y2 + 1) * (w + 1) + x1]
                + intImg[y1 * (w + 1) + x1];
      
      const mean = sum / count;
      const pixel = src[y * w + x];
      result[y * w + x] = pixel < (mean - C) ? 255 : 0;
    }
  }
  return result;
}

function extractContours(binary, edges, w, h) {
  const visited = new Uint8Array(w * h);
  const opaqueContours = [];
  const clearContours = [];

  for (let y = 2; y < h - 2; y++) {
    for (let x = 2; x < w - 2; x++) {
      const idx = y * w + x;
      if (visited[idx]) continue;
      if (edges[idx] !== 255) continue;

      const contour = traceContour(binary, edges, visited, x, y, w, h);
      if (contour && contour.length >= 6) {
        const center = polygonCentroid(contour);
        const cx = Math.floor(center.x);
        const cy = Math.floor(center.y);
        if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
          const inVal = binary[cy * w + cx];
          if (inVal === 255) {
            opaqueContours.push(contour);
          } else {
            clearContours.push(contour);
          }
        }
      }
    }
  }
  return { opaqueContours, clearContours };
}

function traceContour(binary, edges, visited, startX, startY, w, h) {
  const contour = [];
  const directions = [
    [1, 0], [1, 1], [0, 1], [-1, 1],
    [-1, 0], [-1, -1], [0, -1], [1, -1]
  ];

  let x = startX;
  let y = startY;
  let dirIdx = 0;
  let safety = 0;
  const maxSteps = w * h;

  while (safety++ < maxSteps) {
    const idx = y * w + x;
    if (x === startX && y === startY && contour.length > 2) {
      return simplifyContour(contour, 1.0);
    }
    if (visited[idx]) {
      return contour.length > 3 ? simplifyContour(contour, 1.0) : null;
    }

    visited[idx] = 1;
    contour.push({ x, y });

    let found = false;
    for (let i = 0; i < 8; i++) {
      const checkDir = (dirIdx + i) % 8;
      const [dx, dy] = directions[checkDir];
      const nx = x + dx;
      const ny = y + dy;
      const nIdx = ny * w + nx;
      if (nx >= 0 && nx < w && ny >= 0 && ny < h && edges[nIdx] === 255 && !visited[nIdx]) {
        x = nx;
        y = ny;
        dirIdx = (checkDir + 5) % 8;
        found = true;
        break;
      }
    }
    if (!found) break;
  }
  return contour.length > 3 ? simplifyContour(contour, 1.0) : null;
}

function simplifyContour(pts, tolerance) {
  if (pts.length < 3) return pts;
  const result = [pts[0]];
  for (let i = 1; i < pts.length - 1; i++) {
    const prev = result[result.length - 1];
    const curr = pts[i];
    const dx = curr.x - prev.x;
    const dy = curr.y - prev.y;
    if (dx * dx + dy * dy >= tolerance * tolerance) {
      result.push(curr);
    }
  }
  result.push(pts[pts.length - 1]);
  return result;
}

function subpixelRefineJS(contour, gray, w, h, type) {
  const refined = [];
  const radius = 2;
  
  for (const pt of contour) {
    if (pt.x < radius + 1 || pt.x > w - radius - 2 || pt.y < radius + 1 || pt.y > h - radius - 2) {
      refined.push({ x: pt.x, y: pt.y });
      continue;
    }

    let gradX = 0, gradY = 0, weightSum = 0;
    for (let dy = -radius; dy <= radius; dy++) {
      for (let dx = -radius; dx <= radius; dx++) {
        const gx = gray[(pt.y + dy) * w + (pt.x + dx + 1)] - gray[(pt.y + dy) * w + (pt.x + dx - 1)];
        const gy = gray[(pt.y + dy + 1) * w + (pt.x + dx)] - gray[(pt.y + dy - 1) * w + (pt.x + dx)];
        const mag = Math.sqrt(gx * gx + gy * gy);
        const weight = mag * mag;
        gradX += gx * weight;
        gradY += gy * weight;
        weightSum += weight;
      }
    }

    if (weightSum < 0.001) {
      refined.push({ x: pt.x, y: pt.y });
      continue;
    }

    gradX /= weightSum;
    gradY /= weightSum;
    const gradMag = Math.sqrt(gradX * gradX + gradY * gradY);
    if (gradMag < 0.001) {
      refined.push({ x: pt.x, y: pt.y });
      continue;
    }

    const nx = -gradY / gradMag;
    const ny = gradX / gradMag;
    let offset = 0;
    let bestMatch = -1;

    const centerVal = gray[pt.y * w + pt.x];
    const searchDist = 1.0;
    const steps = 10;

    for (let i = -steps; i <= steps; i++) {
      const t = (i / steps) * searchDist;
      const sx = Math.round(pt.x + nx * t);
      const sy = Math.round(pt.y + ny * t);
      if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
      const val = gray[sy * w + sx];
      const score = type === 'opaque' ? Math.abs(val - 128) : Math.abs(val - 128);
      if (score > bestMatch) {
        bestMatch = score;
        offset = t;
      }
    }

    refined.push({
      x: pt.x + nx * offset + 0.5,
      y: pt.y + ny * offset + 0.5
    });
  }
  return refined;
}

function polygonArea(pts) {
  if (pts.length < 3) return 0;
  let area = 0;
  const n = pts.length;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    area += pts[i].x * pts[j].y;
    area -= pts[j].x * pts[i].y;
  }
  return Math.abs(area) / 2;
}

function polygonCentroid(pts) {
  if (pts.length < 3) return pts[0] || { x: 0, y: 0 };
  let cx = 0, cy = 0, a = 0;
  const n = pts.length;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    const cross = pts[i].x * pts[j].y - pts[j].x * pts[i].y;
    a += cross;
    cx += (pts[i].x + pts[j].x) * cross;
    cy += (pts[i].y + pts[j].y) * cross;
  }
  a *= 3;
  return { x: cx / a, y: cy / a };
}

function polygonBBox(pts) {
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const p of pts) {
    if (p.x < minX) minX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.x > maxX) maxX = p.x;
    if (p.y > maxY) maxY = p.y;
  }
  return { minX, minY, maxX, maxY, width: maxX - minX, height: maxY - minY };
}

function fallbackGenerateRasterScan(polygonPoints, options) {
  console.log('[Fallback] Generating JS raster scan path...');
  const stepX = options.stepX || 2;
  const stepY = options.stepY || 2;
  const overlap = options.overlap || 0.3;
  
  const bbox = polygonBBox(polygonPoints);
  const xStart = Math.floor(bbox.minX) - 1;
  const xEnd = Math.ceil(bbox.maxX) + 1;
  const yStart = Math.floor(bbox.minY) - 1;
  const yEnd = Math.ceil(bbox.maxY) + 1;

  const effectiveStepX = stepX * (1 - overlap);
  const scanLines = [];
  let totalPoints = 0;
  let direction = 1;

  for (let y = yStart; y <= yEnd; y += stepY) {
    const intersections = [];
    const n = polygonPoints.length;
    
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      const p1 = polygonPoints[i];
      const p2 = polygonPoints[j];
      
      if ((p1.y <= y && p2.y > y) || (p2.y <= y && p1.y > y)) {
        const t = (y - p1.y) / (p2.y - p1.y);
        const xIntersect = p1.x + t * (p2.x - p1.x);
        intersections.push(xIntersect);
      }
    }
    
    intersections.sort((a, b) => a - b);
    
    if (intersections.length >= 2) {
      for (let i = 0; i < intersections.length - 1; i += 2) {
        const x1 = intersections[i];
        const x2 = intersections[i + 1];
        const linePts = [];
        
        if (direction === 1) {
          for (let x = x1; x <= x2; x += effectiveStepX) {
            linePts.push({ x, y, dose: 1.0, material: 'target' });
          }
        } else {
          for (let x = x2; x >= x1; x -= effectiveStepX) {
            linePts.push({ x, y, dose: 1.0, material: 'target' });
          }
        }
        
        if (linePts.length > 0) {
          scanLines.push({
            y,
            direction,
            points: linePts,
            startX: linePts[0].x,
            endX: linePts[linePts.length - 1].x
          });
          totalPoints += linePts.length;
        }
        direction *= -1;
      }
    }
  }

  return {
    success: true,
    native: false,
    scanLines,
    totalPoints,
    meta: {
      bbox,
      stepX,
      stepY,
      overlap,
      scanLineCount: scanLines.length
    }
  };
}

function fallbackBuildDoseMatrix(scanLines, materialMap, options) {
  console.log('[Fallback] Building JS dose matrix...');
  const beamVoltage = options.beamVoltage || 30;
  const baseDwellTime = options.baseDwellTime || 500;
  const calibration = options.calibration || {
    opaque: { multiplier: 1.2, etchDepth: 25 },
    clear: { multiplier: 0.8, etchDepth: 15 },
    border: { multiplier: 0.6, etchDepth: 10 }
  };

  const commandArray = [];
  let totalDose = 0;
  let maxDwell = 0;

  for (const line of scanLines.scanLines || scanLines) {
    for (const pt of line.points || []) {
      const matKey = pt.material || 'target';
      const matCal = calibration[matKey] || calibration.opaque;
      
      const dwellTime = Math.round(baseDwellTime * (pt.dose || 1.0) * matCal.multiplier);
      const xVoltage = (pt.x / 4096) * beamVoltage;
      const yVoltage = (pt.y / 4096) * beamVoltage;
      const zVoltage = matCal.etchDepth * (pt.dose || 1.0) * 0.01;
      
      const cmd = {
        x: pt.x,
        y: pt.y,
        z: zVoltage,
        xVoltage: +xVoltage.toFixed(6),
        yVoltage: +yVoltage.toFixed(6),
        zVoltage: +zVoltage.toFixed(6),
        dwellTime,
        dose: +(dwellTime * beamVoltage * 0.001).toFixed(4),
        material: matKey,
        layer: options.layer || 0
      };
      
      commandArray.push(cmd);
      totalDose += cmd.dose;
      if (dwellTime > maxDwell) maxDwell = dwellTime;
    }
  }

  return {
    success: true,
    native: false,
    commands: commandArray,
    stats: {
      totalCommands: commandArray.length,
      totalDose: +totalDose.toFixed(2),
      maxDwellTime: maxDwell,
      avgDwellTime: commandArray.length > 0 ? Math.round(totalDose / commandArray.length) : 0
    }
  };
}

function fallbackProcessMultiLayer(layers, options) {
  console.log('[Fallback] Processing JS multi-layer...');
  const results = [];
  const allDefects = { opaqueDefects: [], clearDefects: [] };

  for (let i = 0; i < layers.length; i++) {
    const layer = layers[i];
    const detection = fallbackDetectDefects(layer, { ...options, layer: i });
    results.push({
      layerIndex: i,
      layerName: layer.name || `Layer_${i}`,
      detection
    });
    
    for (const d of detection.opaqueDefects) {
      allDefects.opaqueDefects.push({ ...d, layer: i });
    }
    for (const d of detection.clearDefects) {
      allDefects.clearDefects.push({ ...d, layer: i });
    }
  }

  return {
    success: true,
    native: false,
    layerResults: results,
    combinedDefects: allDefects,
    summary: {
      totalLayers: layers.length,
      totalOpaque: allDefects.opaqueDefects.length,
      totalClear: allDefects.clearDefects.length
    }
  };
}

function fallbackSubpixelRefine(contour, imageData, options) {
  const { width, height } = imageData;
  const pixels = imageData.data;
  const gray = new Float32Array(width * height);
  
  for (let i = 0; i < width * height; i++) {
    const idx = i * 4;
    gray[i] = 0.299 * pixels[idx] + 0.587 * pixels[idx + 1] + 0.114 * pixels[idx + 2];
  }
  
  const blurred = gaussianBlur(gray, width, height, 1.0);
  return subpixelRefineJS(contour, blurred, width, height, options.type || 'opaque');
}
