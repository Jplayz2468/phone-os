const { app, BrowserWindow } = require('electron');

app.commandLine.appendSwitch('--force-device-scale-factor', '1');
app.commandLine.appendSwitch('--high-dpi-support', '1');
app.commandLine.appendSwitch('--no-sandbox');
app.commandLine.appendSwitch('--disable-gpu');
app.commandLine.appendSwitch('--disable-gpu-sandbox');
app.commandLine.appendSwitch('--disable-software-rasterizer');
app.commandLine.appendSwitch('--disable-gpu-compositing');
app.commandLine.appendSwitch('--disable-features', 'VizDisplayCompositor');
app.commandLine.appendSwitch('--use-gl', 'swiftshader');

app.whenReady().then(() => {
  const { screen } = require('electron');
  const primaryDisplay = screen.getPrimaryDisplay();
  const { width, height } = primaryDisplay.workAreaSize;
  
  console.log('Detected screen size:', width, 'x', height);

  const win = new BrowserWindow({
    width: width,
    height: height,
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
      hardwareAcceleration: false,
      zoomFactor: 1.0
    },
    show: false
  });

  win.once('ready-to-show', () => {
    win.show();
    win.focus();
    win.setFullScreen(true);
    win.setKiosk(true);
    win.webContents.setZoomLevel(0);
    win.webContents.setZoomFactor(1.0);
    
    // Force window to detected screen size
    win.setSize(width, height);
    win.setPosition(0, 0);
  });

  win.loadFile('index.html');

  win.on('close', (e) => {
    e.preventDefault();
  });
});

app.on('window-all-closed', () => {
  // Don't quit
});