const { app, BrowserWindow } = require('electron');

// CRITICAL: Force device scale factor to prevent scaling issues
app.commandLine.appendSwitch('--force-device-scale-factor', '1');
app.commandLine.appendSwitch('--high-dpi-support', '1');
app.commandLine.appendSwitch('--no-sandbox');
app.commandLine.appendSwitch('--disable-gpu');
app.commandLine.appendSwitch('--disable-gpu-sandbox');
app.commandLine.appendSwitch('--disable-software-rasterizer');
app.commandLine.appendSwitch('--disable-gpu-compositing');
app.commandLine.appendSwitch('--disable-features', 'VizDisplayCompositor');
app.commandLine.appendSwitch('--use-gl', 'swiftshader');
app.commandLine.appendSwitch('--disable-dev-shm-usage');
app.commandLine.appendSwitch('--no-first-run');
app.commandLine.appendSwitch('--disable-default-apps');
app.commandLine.appendSwitch('--disable-extensions');

app.whenReady().then(() => {
  const win = new BrowserWindow({
    width: 1080,
    height: 1920,
    x: 0,
    y: 0,
    fullscreen: true,
    frame: false,
    transparent: false,
    resizable: false,
    alwaysOnTop: true,
    kiosk: true,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
      webSecurity: false,
      backgroundThrottling: false,
      offscreen: false,
      hardwareAcceleration: false,
      enableRemoteModule: true,
      zoomFactor: 1.0  // Force zoom factor to 1.0
    },
    show: false
  });

  // Force the window to exact dimensions
  win.setSize(1080, 1920);
  win.setPosition(0, 0);

  win.once('ready-to-show', () => {
    win.show();
    win.focus();
    win.setFullScreen(true);
    win.setKiosk(true);
    
    // Set zoom level to 0 (which means 100% zoom)
    win.webContents.setZoomLevel(0);
    win.webContents.setZoomFactor(1.0);
    
    setTimeout(() => {
      win.setSize(1080, 1920);
      win.setPosition(0, 0);
      win.setFullScreen(true);
    }, 500);
  });

  win.loadFile('index.html');

  // Debug window size
  win.webContents.once('dom-ready', () => {
    win.webContents.executeJavaScript(`
      console.log('Window size:', window.innerWidth, 'x', window.innerHeight);
      console.log('Screen size:', screen.width, 'x', screen.height);
      console.log('Device pixel ratio:', window.devicePixelRatio);
    `);
  });

  // Prevent window closure
  win.on('close', (e) => {
    e.preventDefault();
  });
});

app.on('window-all-closed', () => {
  // Don't quit
});