const { app, BrowserWindow } = require('electron');

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
    width: 1080,              // Portrait: width is smaller
    height: 1920,             // Portrait: height is larger  
    x: 0,
    y: 0,
    fullscreen: true,
    frame: false,
    transparent: false,
    resizable: false,
    alwaysOnTop: true,
    kiosk: true,              // Kiosk mode for embedded systems
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
      webSecurity: false,
      backgroundThrottling: false,
      offscreen: false,
      hardwareAcceleration: false,  // Disable hardware acceleration
      enableRemoteModule: true
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
    
    // Additional fullscreen enforcement
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