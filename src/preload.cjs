'use strict';
const {contextBridge, ipcRenderer} = require('electron');
contextBridge.exposeInMainWorld('causalis', Object.freeze({
  invoke: (action, payload = {}) => ipcRenderer.invoke('causalis:invoke', action, payload),
  subscribeState: (callback) => {
    if (typeof callback !== 'function') throw new TypeError('A callback is required');
    const listener = (_event, state) => callback(state);
    ipcRenderer.on('causalis:state', listener);
    return () => ipcRenderer.removeListener('causalis:state', listener);
  },
  subscribeFocusAddress: (callback) => {
    if (typeof callback !== 'function') throw new TypeError('A callback is required');
    const listener = () => callback();
    ipcRenderer.on('browser:focus-address', listener);
    return () => ipcRenderer.removeListener('browser:focus-address', listener);
  }
}));
