const { app, BrowserWindow, ipcMain, dialog, Menu } = require('electron');
const path = require('path');
const fs = require('fs');

let mainWindow = null;
let processingWorker = null;

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1920,
    height: 1080,
    minWidth: 1280,
    minHeight: 720,
    backgroundColor: '#0a0e17',
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
      webSecurity: true,
      backgroundThrottling: false
    },
    frame: true,
    title: 'E-Beam Reticle Repair System v1.0',
    icon: path.join(__dirname, '../public/icon.png')
  });

  const isDev = process.env.NODE_ENV === 'development';
  
  if (isDev) {
    mainWindow.loadURL('http://localhost:8080');
    mainWindow.webContents.openDevTools({ mode: 'detach' });
  } else {
    mainWindow.loadFile(path.join(__dirname, '../dist/index.html'));
  }

  mainWindow.once('ready-to-show', () => {
    mainWindow.show();
    mainWindow.maximize();
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
  });

  createApplicationMenu();
}

function createApplicationMenu() {
  const template = [
    {
      label: '文件',
      submenu: [
        {
          label: '加载 SEM 图像...',
          accelerator: 'Ctrl+O',
          click: () => {
            mainWindow.webContents.send('menu:load-image');
          }
        },
        {
          label: '加载多层图像...',
          accelerator: 'Ctrl+Shift+O',
          click: () => {
            mainWindow.webContents.send('menu:load-multi-layer');
          }
        },
        { type: 'separator' },
        {
          label: '导出修复指令',
          accelerator: 'Ctrl+E',
          click: () => {
            mainWindow.webContents.send('menu:export-commands');
          }
        },
        {
          label: '导出 SVG 轮廓',
          accelerator: 'Ctrl+S',
          click: () => {
            mainWindow.webContents.send('menu:export-svg');
          }
        },
        { type: 'separator' },
        {
          label: '退出',
          accelerator: 'Alt+F4',
          role: 'quit'
        }
      ]
    },
    {
      label: '处理',
      submenu: [
        {
          label: '运行缺陷检测',
          accelerator: 'F5',
          click: () => {
            mainWindow.webContents.send('menu:run-detection');
          }
        },
        {
          label: '生成扫描路径',
          accelerator: 'F6',
          click: () => {
            mainWindow.webContents.send('menu:generate-scan-path');
          }
        },
        {
          label: '执行修复模拟',
          accelerator: 'F7',
          click: () => {
            mainWindow.webContents.send('menu:run-simulation');
          }
        }
      ]
    },
    {
      label: '视图',
      submenu: [
        { role: 'reload', label: '刷新' },
        { role: 'toggleDevTools', label: '开发者工具' },
        { type: 'separator' },
        { role: 'resetZoom', label: '重置缩放' },
        { role: 'zoomIn', label: '放大' },
        { role: 'zoomOut', label: '缩小' },
        { type: 'separator' },
        { role: 'togglefullscreen', label: '全屏' }
      ]
    },
    {
      label: '帮助',
      submenu: [
        {
          label: '系统信息',
          click: () => {
            dialog.showMessageBox(mainWindow, {
              type: 'info',
              title: '系统信息',
              message: 'E-Beam Reticle Repair System',
              detail: `版本: 1.0.0\n平台: ${process.platform}\n架构: ${process.arch}\nNode.js: ${process.version}\nElectron: ${process.versions.electron}\n\n© 2026 Semiconductor Equipment R&D`
            });
          }
        }
      ]
    }
  ];

  const menu = Menu.buildFromTemplate(template);
  Menu.setApplicationMenu(menu);
}

app.whenReady().then(() => {
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

// ==================== IPC Handlers ====================

ipcMain.handle('dialog:open-file', async (event, options = {}) => {
  const result = await dialog.showOpenDialog(mainWindow, {
    title: options.title || '选择文件',
    filters: options.filters || [
      { name: 'SEM 图像', extensions: ['tif', 'tiff', 'png', 'bmp', 'jpg', 'jpeg', 'raw'] },
      { name: '所有文件', extensions: ['*'] }
    ],
    properties: options.properties || ['openFile']
  });
  return result;
});

ipcMain.handle('dialog:open-files', async (event, options = {}) => {
  const result = await dialog.showOpenDialog(mainWindow, {
    title: options.title || '选择多层图像',
    filters: options.filters || [
      { name: 'SEM 图像', extensions: ['tif', 'tiff', 'png', 'bmp', 'jpg', 'jpeg'] },
      { name: '所有文件', extensions: ['*'] }
    ],
    properties: ['openFile', 'multiSelections']
  });
  return result;
});

ipcMain.handle('dialog:save-file', async (event, options = {}) => {
  const result = await dialog.showSaveDialog(mainWindow, {
    title: options.title || '保存文件',
    defaultPath: options.defaultPath || '',
    filters: options.filters || [{ name: '所有文件', extensions: ['*'] }]
  });
  return result;
});

ipcMain.handle('fs:read-file', async (event, filePath) => {
  try {
    const buffer = fs.readFileSync(filePath);
    return { success: true, data: buffer.toString('base64'), path: filePath };
  } catch (err) {
    return { success: false, error: err.message };
  }
});

ipcMain.handle('fs:read-image-buffer', async (event, filePath) => {
  try {
    const buffer = fs.readFileSync(filePath);
    return { success: true, data: Array.from(buffer), path: filePath, size: buffer.length };
  } catch (err) {
    return { success: false, error: err.message };
  }
});

ipcMain.handle('fs:write-file', async (event, filePath, data, encoding = 'utf8') => {
  try {
    if (encoding === 'base64') {
      fs.writeFileSync(filePath, Buffer.from(data, 'base64'));
    } else if (encoding === 'json') {
      fs.writeFileSync(filePath, JSON.stringify(data, null, 2), 'utf8');
    } else {
      fs.writeFileSync(filePath, data, encoding);
    }
    return { success: true, path: filePath };
  } catch (err) {
    return { success: false, error: err.message };
  }
});

ipcMain.handle('system:get-info', async () => {
  return {
    platform: process.platform,
    arch: process.arch,
    nodeVersion: process.version,
    electronVersion: process.versions.electron,
    chromeVersion: process.versions.chrome,
    cwd: process.cwd(),
    resourcesPath: process.resourcesPath
  };
});

ipcMain.on('log:info', (event, ...args) => {
  console.log('[INFO]', new Date().toISOString(), ...args);
});

ipcMain.on('log:error', (event, ...args) => {
  console.error('[ERROR]', new Date().toISOString(), ...args);
});

ipcMain.on('log:warn', (event, ...args) => {
  console.warn('[WARN]', new Date().toISOString(), ...args);
});
