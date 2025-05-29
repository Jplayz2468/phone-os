const { app, BrowserWindow } = require('electron');

app.whenReady().then(() => {
  const win = new BrowserWindow({
    fullscreen: true,         // forces true fullscreen
    frame: false,             // removes window border
    transparent: false,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
    },
    cursor: 'none' // this doesn't work directly — handled in CSS
  });

  win.loadFile('index.html');
});
