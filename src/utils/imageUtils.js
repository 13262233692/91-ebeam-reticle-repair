export function loadImageFromFile(filePath) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      const canvas = document.createElement('canvas');
      canvas.width = img.naturalWidth;
      canvas.height = img.naturalHeight;
      const ctx = canvas.getContext('2d');
      ctx.drawImage(img, 0, 0);
      const imgData = ctx.getImageData(0, 0, canvas.width, canvas.height);
      resolve({
        width: canvas.width,
        height: canvas.height,
        channels: 4,
        data: imgData.data,
        canvas,
        ctx,
        naturalWidth: img.naturalWidth,
        naturalHeight: img.naturalHeight,
        src: img.src
      });
    };
    img.onerror = (e) => reject(e);
    img.src = filePath;
  });
}

export function loadImageFromBuffer(buffer, width, height, channels = 4) {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d');
  const imgData = ctx.createImageData(width, height);
  
  if (channels === 1) {
    for (let i = 0, j = 0; i < buffer.length; i++, j += 4) {
      imgData.data[j] = buffer[i];
      imgData.data[j + 1] = buffer[i];
      imgData.data[j + 2] = buffer[i];
      imgData.data[j + 3] = 255;
    }
  } else {
    imgData.data.set(buffer);
  }
  
  ctx.putImageData(imgData, 0, 0);
  return {
    width, height, channels: 4,
    data: imgData.data,
    canvas, ctx
  };
}

export function getPixelGray(imageData, x, y) {
  const idx = (y * imageData.width + x) * 4;
  const d = imageData.data;
  return 0.299 * d[idx] + 0.587 * d[idx + 1] + 0.114 * d[idx + 2];
}

export function createEmptyLog() {
  return [];
}

export function addLog(logs, msg, type = 'info') {
  const time = new Date().toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' }) + '.' + String(new Date().getMilliseconds()).padStart(3, '0');
  return [...logs, { time, msg, type, id: Date.now() + Math.random() }];
}

export function scanPathToLineData(scanLines) {
  if (!scanLines || !scanLines.length) return [];
  const allPts = [];
  for (const line of scanLines) {
    if (!line.points) continue;
    for (const pt of line.points) {
      allPts.push({ x: pt.x, y: pt.y, z: pt.z || 0, dose: pt.dose || 1, material: pt.material || 'target' });
    }
  }
  return allPts;
}

export function commandsToPathData(commands) {
  if (!commands || !commands.length) return { lines: [], points: [] };
  const lines = [];
  let currentLine = [commands[0]];
  
  for (let i = 1; i < commands.length; i++) {
    const prev = commands[i - 1];
    const curr = commands[i];
    if (Math.abs(curr.y - prev.y) < 0.5 && curr.x !== prev.x) {
      currentLine.push(curr);
    } else {
      if (currentLine.length > 1) lines.push(currentLine);
      currentLine = [curr];
    }
  }
  if (currentLine.length > 1) lines.push(currentLine);
  
  return { lines, points: commands };
}

export function debounce(fn, wait = 100) {
  let timer = null;
  return function(...args) {
    if (timer) clearTimeout(timer);
    timer = setTimeout(() => fn.apply(this, args), wait);
  };
}

export function throttle(fn, limit = 50) {
  let inThrottle = false;
  return function(...args) {
    if (!inThrottle) {
      fn.apply(this, args);
      inThrottle = true;
      setTimeout(() => inThrottle = false, limit);
    }
  };
}

export function formatNumber(n, decimals = 2) {
  if (typeof n !== 'number' || isNaN(n)) return '0.00';
  return n.toFixed(decimals);
}

export function formatArea(areaPx, pixelSizeNm = 1.0) {
  if (typeof areaPx !== 'number' || isNaN(areaPx)) return '0 px²';
  if (areaPx < 1e6) {
    return `${formatNumber(areaPx, 1)} px²`;
  }
  const areaNm2 = areaPx * pixelSizeNm * pixelSizeNm;
  if (areaNm2 < 1e6) {
    return `${formatNumber(areaNm2, 1)} nm²`;
  }
  return `${formatNumber(areaNm2 / 1e6, 2)} μm²`;
}
