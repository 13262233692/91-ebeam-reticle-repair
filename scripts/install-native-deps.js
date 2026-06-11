const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

console.log('='.repeat(60));
console.log('  E-Beam Reticle Repair System - Native Dependencies Setup');
console.log('='.repeat(60));

const root = path.resolve(__dirname, '..');
const nativeSrc = path.join(root, 'src');
const buildDir = path.join(root, 'build');

console.log(`\n[1/5] 项目根目录: ${root}`);
console.log(`[2/5] C++ 源码目录: ${nativeSrc}`);
console.log(`[3/5] 构建输出目录: ${buildDir}`);

function checkCommand(cmd, name) {
  try {
    execSync(cmd, { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}

const hasPython = checkCommand('python --version 2>NUL || python3 --version', 'Python');
const hasNodeGyp = checkCommand('node-gyp --version 2>NUL', 'node-gyp');
console.log(`\n[4/5] 环境检测:`);
console.log(`  ✓ Python (node-gyp 依赖): ${hasPython ? '已检测' : '未安装 (需 Python 3.x)'}`);
console.log(`  ✓ node-gyp: ${hasNodeGyp ? '已检测' : '未安装 (npm install -g node-gyp)'}`);

let openCVFound = false;
const possibleCVPaths = [
  'C:/opencv',
  'C:/tools/opencv',
  'C:/dev/opencv',
  process.env.OPENCV_DIR,
  process.env.OpenCV_DIR
].filter(Boolean);

for (const p of possibleCVPaths) {
  if (p && fs.existsSync(p)) {
    const cvWorld = path.join(p, 'build/x64/vc16/lib/opencv_world480.lib');
    const cvInclude = path.join(p, 'build/include');
    if (fs.existsSync(cvInclude)) {
      openCVFound = true;
      console.log(`  ✓ OpenCV 路径: ${p}`);
      if (fs.existsSync(cvWorld)) {
        console.log(`    · 预编译库存在: opencv_world480.lib`);
      } else {
        console.log(`    · 建议自行编译 opencv_world 库`);
      }
      break;
    }
  }
}

if (!openCVFound) {
  console.log(`  ⚠ OpenCV 未自动检测到 (binding.gyp 中默认为 C:/opencv)`);
  console.log(`    请修改 binding.gyp 中的 include_dirs 和 libraries 路径`);
  console.log(`    或设置环境变量 OPENCV_DIR`);
}

console.log(`\n[5/5] 编译提示:`);
console.log(`  ➜ 开发模式构建:  npm run build:native`);
console.log(`  ➜ 如遇 OpenCV 路径问题，请修改 binding.gyp 前三处路径`);
console.log(`  ➜ 若无法编译 C++ 模块，系统将自动使用 JS 回退算法`);
console.log();
console.log('  JS Fallback 已完整实现以下功能:');
console.log('    · 高斯滤波降噪');
console.log('    · 自适应阈值分割');
console.log('    · Sobel + NMS + 双阈值 Canny 边缘');
console.log('    · 轮廓提取与面积筛选');
console.log('    · 亚像素插值轮廓精修');
console.log('    · Raster / Contour / Hybrid 扫描路径');
console.log('    · 剂量矩阵与曝光指令生成');
console.log();
console.log('='.repeat(60));
console.log('  Setup complete. Run: npm install && npm run dev');
console.log('='.repeat(60));
