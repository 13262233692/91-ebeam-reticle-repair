export function contourToSvgPoints(points) {
  if (!points || !points.length) return '';
  return points.map(p => `${p.x.toFixed(2)},${p.y.toFixed(2)}`).join(' ');
}

export function pointsToSvgPolygon(points) {
  return contourToSvgPoints(points);
}

export function generateDefectSvg(defect, selected = false, hovered = false) {
  const fill = defect.type === 'opaque' 
    ? (selected ? 'rgba(255,107,107,0.35)' : 'rgba(255,107,107,0.18)')
    : (selected ? 'rgba(78,205,196,0.35)' : 'rgba(78,205,196,0.18)');
  const stroke = defect.type === 'opaque' ? '#ff6b6b' : '#4ecdc4';
  const strokeWidth = selected ? 2 : (hovered ? 1.8 : 1.2);
  const dashArray = defect.type === 'clear' ? '4 2' : 'none';
  const pointsStr = contourToSvgPoints(defect.points);
  
  return {
    fill, stroke, strokeWidth, dashArray, pointsStr,
    glow: selected || hovered
  };
}

export function buildSvgDef(defects, bbox) {
  let svg = `<?xml version="1.0" encoding="UTF-8"?>\n`;
  svg += `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${bbox.minX} ${bbox.minY} ${bbox.width} ${bbox.height}" width="${bbox.width}" height="${bbox.height}">\n`;
  svg += `  <defs>\n`;
  svg += `    <filter id="glow-opaque" x="-50%" y="-50%" width="200%" height="200%">\n`;
  svg += `      <feGaussianBlur stdDeviation="2" result="coloredBlur"/>\n`;
  svg += `      <feMerge><feMergeNode in="coloredBlur"/><feMergeNode in="SourceGraphic"/></feMerge>\n`;
  svg += `    </filter>\n`;
  svg += `    <filter id="glow-clear" x="-50%" y="-50%" width="200%" height="200%">\n`;
  svg += `      <feGaussianBlur stdDeviation="2" result="coloredBlur"/>\n`;
  svg += `      <feMerge><feMergeNode in="coloredBlur"/><feMergeNode in="SourceGraphic"/></feMerge>\n`;
  svg += `    </filter>\n`;
  svg += `  </defs>\n`;
  svg += `  <rect x="${bbox.minX}" y="${bbox.minY}" width="${bbox.width}" height="${bbox.height}" fill="none" stroke="#2a3a5a" stroke-width="1"/>\n`;
  
  defects.forEach((d, i) => {
    const pts = contourToSvgPoints(d.points);
    const filter = d.type === 'opaque' ? 'filter="url(#glow-opaque)"' : 'filter="url(#glow-clear)"';
    if (d.type === 'opaque') {
      svg += `  <polygon id="${d.id}" data-type="opaque" data-index="${i}" points="${pts}" fill="rgba(255,107,107,0.25)" stroke="#ff6b6b" stroke-width="1.5" ${filter}/>\n`;
    } else {
      svg += `  <polygon id="${d.id}" data-type="clear" data-index="${i}" points="${pts}" fill="rgba(78,205,196,0.25)" stroke="#4ecdc4" stroke-width="1.5" stroke-dasharray="4 2" ${filter}/>\n`;
    }
  });
  
  svg += `</svg>\n`;
  return svg;
}

export function formatArea(px) {
  if (px < 1000) return px.toFixed(1) + ' px²';
  if (px < 1e6) return (px / 1e3).toFixed(2) + ' Kpx²';
  return (px / 1e6).toFixed(3) + ' Mpx²';
}

export function formatLength(px) {
  if (px < 1000) return px.toFixed(1) + ' px';
  return (px / 1000).toFixed(2) + ' Kpx';
}

export function formatDwellTime(ns) {
  if (ns < 1000) return ns + ' ns';
  if (ns < 1e6) return (ns / 1000).toFixed(1) + ' μs';
  return (ns / 1e6).toFixed(2) + ' ms';
}

export function formatDose(nc) {
  if (nc < 1000) return nc.toFixed(1) + ' nC';
  if (nc < 1e6) return (nc / 1000).toFixed(2) + ' μC';
  return (nc / 1e6).toFixed(3) + ' mC';
}

export function clamp(val, min, max) {
  return Math.max(min, Math.min(max, val));
}

export function lerp(a, b, t) {
  return a + (b - a) * t;
}

export function getBBoxCenter(bbox) {
  return {
    x: (bbox.minX + bbox.maxX) / 2,
    y: (bbox.minY + bbox.maxY) / 2
  };
}

export function getBBoxFromDefects(defects) {
  if (!defects || !defects.length) return { minX: 0, minY: 0, maxX: 1, maxY: 1, width: 1, height: 1 };
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const d of defects) {
    const b = d.boundingBox || d.bbox;
    if (!b) continue;
    if (b.minX < minX) minX = b.minX;
    if (b.minY < minY) minY = b.minY;
    if (b.maxX > maxX) maxX = b.maxX;
    if (b.maxY > maxY) maxY = b.maxY;
  }
  const pad = Math.max((maxX - minX), (maxY - minY)) * 0.1;
  minX -= pad; minY -= pad; maxX += pad; maxY += pad;
  return { minX, minY, maxX, maxY, width: maxX - minX, height: maxY - minY };
}
