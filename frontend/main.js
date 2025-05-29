const { app, BrowserWindow } = require('electron');

app.whenReady().then(() => {
  const win = new BrowserWindow({
    fullscreen: true,
    webPreferences: { contextIsolation: false }
  });
  win.loadFile('frontend/index.html');
});
