const { app, BrowserWindow } = require('electron');

app.whenReady().then(() => {
  const win = new BrowserWindow({
    width: 1920,              // Set explicit dimensions
    height: 1080,
    fullscreen: true,
    frame: false,
    transparent: false,
    resizable: false,
    alwaysOnTop: true,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
      webSecurity: false,
      backgroundThrottling: false,
      offscreen: false
    },
    show: false  // Don't show until ready
  });

  // Show window when ready to prevent flash
  win.once('ready-to-show', () => {
    win.show();
    win.focus();
    
    // Force fullscreen after a brief delay
    setTimeout(() => {
      win.setFullScreen(true);
    }, 100);
  });

  win.loadFile('index.html');

  // Prevent any accidental window closure
  win.on('close', (e) => {
    e.preventDefault();
  });
});

// Prevent app from quitting when all windows are closed
app.on('window-all-closed', () => {
  // Don't quit on embedded systems
});

app.on('activate', () => {
  // Re-create window if needed
});