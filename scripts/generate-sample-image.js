const { createCanvas } = require('canvas');
const fs = require('fs');
const path = require('path');

const W = 2048;
const H = 2048;
const canvas = createCanvas(W, H);
const ctx = canvas.getContext('2d');

ctx.fillStyle = '#404040';
ctx.fillRect(0, 0, W, H);

for (let y = 0; y < H; y += 256) {
  for (let x = 0; x < W; x += 256) {
    const shade = 60 + ((x / 256 + y / 256) % 2) * 8;
    ctx.fillStyle = `rgb(${shade},${shade},${shade})`;
    ctx.fillRect(x, y, 256, 256);
  }
}

const lineShade = 28;
ctx.strokeStyle = `rgb(${lineShade},${lineShade},${lineShade})`;
ctx.lineWidth = 32;
for (let i = 0; i < 20; i++) {
  const y = 150 + i * 96;
  ctx.beginPath();
  ctx.moveTo(50, y);
  ctx.lineTo(W - 50, y);
  ctx.stroke();
}
for (let i = 0; i < 15; i++) {
  const x = 150 + i * 128;
  ctx.beginPath();
  ctx.moveTo(x, 100);
  ctx.lineTo(x, H - 100);
  ctx.stroke();
}

// 添加 Opaque 缺陷 (多余金属 - 亮)
const opaqueDefects = [
  { x: 450, y: 320, r: 45, color: 90 },
  { x: 1100, y: 560, r: 32, color: 100 },
  { x: 780, y: 900, r: 58, color: 85 },
  { x: 1600, y: 1240, r: 28, color: 95 },
  { x: 300, y: 1500, r: 70, color: 80 },
  { x: 1400, y: 1800, r: 40, color: 88 }
];

for (const d of opaqueDefects) {
  const grd = ctx.createRadialGradient(d.x, d.y, 2, d.x, d.y, d.r);
  grd.addColorStop(0, `rgb(${d.color + 30},${d.color + 30},${d.color + 30})`);
  grd.addColorStop(0.6, `rgb(${d.color},${d.color},${d.color})`);
  grd.addColorStop(1, `rgb(${d.color - 20},${d.color - 20},${d.color - 20})`);
  ctx.fillStyle = grd;
  ctx.beginPath();
  const points = 8 + Math.floor(Math.random() * 5);
  for (let i = 0; i <= points; i++) {
    const a = (i / points) * Math.PI * 2;
    const r = d.r * (0.75 + Math.sin(i * 2.3) * 0.18 + Math.cos(i * 1.7) * 0.1);
    const px = d.x + Math.cos(a) * r;
    const py = d.y + Math.sin(a) * r;
    if (i === 0) ctx.moveTo(px, py);
    else ctx.lineTo(px, py);
  }
  ctx.closePath();
  ctx.fill();
}

// 添加 Clear 缺陷 (金属缺失 - 暗)
const clearDefects = [
  { x: 620, y: 220, r: 38, color: 15 },
  { x: 1300, y: 700, r: 50, color: 10 },
  { x: 200, y: 1100, r: 30, color: 12 },
  { x: 950, y: 1400, r: 65, color: 8 },
  { x: 1750, y: 400, r: 25, color: 18 },
  { x: 550, y: 1750, r: 42, color: 11 },
  { x: 1500, y: 1050, r: 36, color: 14 }
];

for (const d of clearDefects) {
  const grd = ctx.createRadialGradient(d.x, d.y, 2, d.x, d.y, d.r);
  grd.addColorStop(0, `rgb(${d.color},${d.color},${d.color})`);
  grd.addColorStop(0.7, `rgb(${d.color + 10},${d.color + 10},${d.color + 10})`);
  grd.addColorStop(1, `rgb(${d.color + 25},${d.color + 25},${d.color + 25})`);
  ctx.fillStyle = grd;
  ctx.beginPath();
  const points = 7 + Math.floor(Math.random() * 5);
  for (let i = 0; i <= points; i++) {
    const a = (i / points) * Math.PI * 2;
    const r = d.r * (0.7 + Math.sin(i * 3.1) * 0.2 + Math.cos(i * 2.2) * 0.15);
    const px = d.x + Math.cos(a) * r;
    const py = d.y + Math.sin(a) * r;
    if (i === 0) ctx.moveTo(px, py);
    else ctx.lineTo(px, py);
  }
  ctx.closePath();
  ctx.fill();
}

const imgData = ctx.getImageData(0, 0, W, H);
for (let i = 0; i < imgData.data.length; i += 4) {
  const noise = (Math.random() - 0.5) * 22;
  const v = Math.max(0, Math.min(255, imgData.data[i] + noise));
  imgData.data[i] = v;
  imgData.data[i + 1] = v;
  imgData.data[i + 2] = v;
}
ctx.putImageData(imgData, 0, 0);

ctx.fillStyle = '#00d4ff';
ctx.font = '28px Consolas, monospace';
ctx.fillText('SEM-IMG-EUV-MASK-LAYER-001', 40, 50);
ctx.fillText(`${W} x ${H} | 16-bit TIFF | Mag: 250kx`, 40, 85);
ctx.strokeStyle = '#00d4ff';
ctx.lineWidth = 2;
ctx.strokeRect(25, 25, W - 50, H - 50);
ctx.beginPath();
ctx.moveTo(W - 200, H - 80);
ctx.lineTo(W - 50, H - 80);
ctx.stroke();
ctx.fillStyle = '#00d4ff';
ctx.font = '20px Consolas';
ctx.fillText('100 nm', W - 160, H - 55);

const outFile = path.join(__dirname, '..', 'sample-sem-image.png');
const buf = canvas.toBuffer('image/png');
fs.writeFileSync(outFile, buf);
console.log(`✓ 示例 SEM 掩膜版图像已生成: ${outFile}`);
console.log(`  尺寸: ${W} x ${H}`);
console.log(`  Opaque 缺陷: ${opaqueDefects.length} 个`);
console.log(`  Clear 缺陷: ${clearDefects.length} 个`);
console.log();
console.log('可直接使用此图像测试系统:');
console.log('  1. npm install');
console.log('  2. npm run dev');
console.log('  3. 点击"加载图像"按钮，选择 sample-sem-image.png');
