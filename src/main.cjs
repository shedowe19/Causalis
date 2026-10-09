'use strict';
const {app, BrowserWindow, ipcMain, protocol, session, dialog, Menu, powerMonitor} = require('electron');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {BrowserController} = require('./browser.cjs');
const {VaultBroker} = require('./vault.cjs');
const {UI_URL, text, workspaceId, trustedEvent, exactHttpsOrigin} = require('./policy.cjs');
const {installClient, clientDestination, readClientHash} = require('./client-install.cjs');

const smoke = process.argv.includes('--smoke-test');
let reportDir;
if (smoke) {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'causalis-smoke-'));
  app.setPath('userData', temporary);
  reportDir = process.env.CAUSALIS_REPORT_DIR || path.join(temporary, 'reports');
  app.disableHardwareAcceleration();
}
app.enableSandbox();
protocol.registerSchemesAsPrivileged([{scheme:'causalis', privileges:{standard:true, secure:true, supportFetchAPI:true, corsEnabled:true}}]);
const uiRoot = path.resolve(__dirname, '../ui');
const MIME = {'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.js':'text/javascript; charset=utf-8'};
const CSP = "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; connect-src 'none'; base-uri 'none'; form-action 'none'; frame-src 'none'; object-src 'none'";
function installProtocol(target, trustedUi = false) {
  target.protocol.handle('causalis', (request) => {
    const url = new URL(request.url);
    let filename;
    if (trustedUi && url.host === 'ui' && ['/index.html','/app.js','/styles.css'].includes(url.pathname)) filename = url.pathname.slice(1);
    if (!trustedUi && url.host === 'start' && (url.pathname === '/' || url.pathname === '/index.html')) filename = 'start.html';
    if (!filename || request.method !== 'GET' || url.username || url.password || url.search) return new Response('Not found', {status:404});
    return new Response(fs.readFileSync(path.join(uiRoot, filename)), {headers:{'Content-Type':MIME[path.extname(filename)], 'Content-Security-Policy':trustedUi ? CSP : "default-src 'none'; style-src 'unsafe-inline'; img-src data:; base-uri 'none'; form-action 'none'; frame-src 'none'", 'X-Content-Type-Options':'nosniff'}});
  });
  target.setPermissionRequestHandler((_contents,_permission,callback)=>callback(false));
  target.setPermissionCheckHandler(()=>false);
}

let window, browser, vault;
let vaultState = {status:'unavailable',server:'https://vault.bitwarden.com',clientConfigured:false};
let vaultOpen = true, vaultEpoch = 0, notice = '', closing = false;
function targetSnapshot() {
  const target = browser?.activeTarget();
  return target ? {tabId:target.id, epoch:target.epoch, origin:exactHttpsOrigin(target.url), url:target.url} : null;
}
function snapshot() {
  const state = browser?.snapshot() || {tabs:[],workspace:'personal'};
  return {...state, activeTabId:state.activeTabId ?? state.activeId, vaultOpen, vault:vaultState, target:targetSnapshot(), notice};
}
function emitState() {
  if (window && !window.isDestroyed() && !window.webContents.isDestroyed()) window.webContents.send('causalis:state', snapshot());
}
async function refreshVault() {
  try { vaultState = await vault.status(); }
  catch { vaultState = {...vaultState,status:'unavailable',clientConfigured:false}; }
  emitState();
  return vaultState;
}
function requireTarget() {
  const target = browser.activeTarget();
  const origin = target && exactHttpsOrigin(target.url);
  if (!origin) throw new Error('Zugangsdaten können nur in HTTPS-Seiten eingefüllt werden.');
  return {...target,origin};
}
async function invoke(event, action, payload = {}) {
  if (!trustedEvent(event, window?.webContents)) throw new Error('Kein Zugriff auf die Browserverwaltung.');
  if (typeof action !== 'string' || !payload || typeof payload !== 'object' || Array.isArray(payload)) throw new Error('Ungültige Anfrage.');
  let result;
  switch (action) {
    case 'browser:snapshot': return snapshot();
    case 'browser:new-tab': result = await browser.createTab(payload.url == null ? undefined : text(payload.url,8192)); break;
    case 'browser:navigate': result = await browser.navigate(text(payload.value,8192,true)); break;
    case 'browser:select-tab': result = await browser.selectTab(payload.id); break;
    case 'browser:close-tab': result = await browser.closeTab(payload.id); break;
    case 'browser:back': result = await browser.back(); break;
    case 'browser:forward': result = await browser.forward(); break;
    case 'browser:reload': result = await browser.reload(); break;
    case 'browser:workspace': {
      const id = workspaceId(payload.id);
      ++vaultEpoch;
      await vault.setWorkspace(id);
      await browser.setWorkspace(id);
      result = await refreshVault(); break;
    }
    case 'browser:layout':
      vaultOpen = payload.vaultOpen === true;
      browser.layout({sidebarWidth:72,toolbarHeight:110,rightPanelWidth:vaultOpen ? 380 : 0});
      break;
    case 'vault:status': result = await refreshVault(); break;
    case 'vault:install-cli': {
      ++vaultEpoch; await vault.lock();
      notice = 'Der offizielle Bitwarden-Client wird heruntergeladen und geprüft.'; emitState();
      const installed = await installClient(app.getPath('userData'));
      ++vaultEpoch;
      await vault.setExecutable(installed.path, installed.sha256);
      notice = 'Bitwarden-Client bereit. Server wählen und anmelden.';
      result = await refreshVault(); break;
    }
    case 'vault:select-cli': {
      const selected = await dialog.showOpenDialog(window,{title:'Offizielle bw.exe auswählen',properties:['openFile'],filters:[{name:'Bitwarden CLI',extensions:['exe']}]});
      if (selected.canceled) return null;
      const confirmation = await dialog.showMessageBox(window,{type:'question',buttons:['Abbrechen','Vertrauenswürdigen Client verwenden'],defaultId:0,cancelId:0,title:'Bitwarden-Client',message:'Dieser Client erhält beim Entsperren dein Masterpasswort.',detail:'Wähle ausschließlich eine unveränderte offizielle bw.exe. Für den geprüften Download verwende „Bitwarden-Client installieren“.'});
      if (confirmation.response !== 1) return null;
      ++vaultEpoch;
      await vault.setExecutable(selected.filePaths[0]);
      result = await refreshVault(); break;
    }
    case 'vault:configure': ++vaultEpoch; result = await vault.configure(text(payload.server,4096,true)); await refreshVault(); break;
    case 'vault:login':
      result = await vault.login({email:text(payload.email || '',320),password:text(payload.password || '',4096),code:text(payload.code || '',512),method:payload.method,clientId:text(payload.clientId || '',1024),clientSecret:text(payload.clientSecret || '',4096)});
      await refreshVault(); break;
    case 'vault:unlock': result = await vault.unlock(text(payload.password,4096,true)); await refreshVault(); break;
    case 'vault:sync': result = await vault.sync(); await refreshVault(); break;
    case 'vault:lock': ++vaultEpoch; result = await vault.lock(); await refreshVault(); break;
    case 'vault:logout': ++vaultEpoch; result = await vault.logout(); await refreshVault(); break;
    case 'vault:matches': {
      const target = requireTarget();
      const stamp = vaultEpoch;
      result = await vault.matches(target.origin);
      const current = requireTarget();
      if (current.id !== target.id || current.epoch !== target.epoch || current.origin !== target.origin || stamp !== vaultEpoch || !vault.isUnlocked()) throw new Error('Die Seite oder Tresorfreigabe hat sich geändert. Erneut auswählen.');
      break;
    }
    case 'vault:fill': {
      const target = requireTarget();
      if (payload.tabId !== target.id || payload.epoch !== target.epoch || payload.origin !== target.origin) throw new Error('Die Seite hat sich geändert. Eintrag erneut auswählen.');
      const stamp = vaultEpoch;
      const credential = await vault.credential(text(payload.id,256,true),target.origin);
      if (stamp !== vaultEpoch || !vault.isUnlocked()) throw new Error('Der Tresor ist gesperrt.');
      const current = requireTarget();
      if (current.id !== target.id || current.epoch !== target.epoch || current.origin !== target.origin) throw new Error('Die Seite hat sich geändert.');
      try {result = await browser.fillCredential({...credential,id:target.id,epoch:target.epoch,origin:target.origin});}
      finally {credential.password = ''; credential.username = '';}
      break;
    }
    case 'vault:create': {
      const target = requireTarget();
      if (payload.origin !== target.origin) throw new Error('Die Zielseite hat sich geändert.');
      result = await vault.createLogin({name:text(payload.name,256,true),username:text(payload.username || '',1024),password:text(payload.password,4096,true),origin:target.origin}); break;
    }
    case 'vault:generate': return vault.generate();
    default: throw new Error('Unbekannte Aktion.');
  }
  emitState();
  return result ?? snapshot();
}

async function bootstrap() {
  const uiSession = session.fromPartition('causalis-ui');
  installProtocol(uiSession,true);
  window = new BrowserWindow({width:1440,height:940,minWidth:960,minHeight:640,show:!smoke,backgroundColor:'#101321',title:'Causalis',webPreferences:{session:uiSession,preload:path.join(__dirname,'preload.cjs'),sandbox:true,contextIsolation:true,nodeIntegration:false,webSecurity:true,webviewTag:false,devTools:false}});
  window.webContents.on('will-navigate',event=>event.preventDefault());
  window.webContents.setWindowOpenHandler(()=>({action:'deny'}));
  browser = new BrowserController({window,onState:emitState,onSession:target=>installProtocol(target,false),startUrl:'causalis://start/',smoke});
  vault = new VaultBroker({dataRoot:path.join(app.getPath('userData'),'vaults'),onChange:()=>{++vaultEpoch;setImmediate(()=>refreshVault());}});
  const clientPath = smoke && process.env.CAUSALIS_CI_BW_PATH ? process.env.CAUSALIS_CI_BW_PATH : clientDestination(app.getPath('userData'));
  if (fs.existsSync(clientPath)) {
    try {
      const checksum = smoke ? undefined : readClientHash(app.getPath('userData'));
      if (!smoke && !checksum) throw new Error('Missing verified client metadata');
      await vault.setExecutable(clientPath,checksum);
    }
    catch {notice='Der Bitwarden-Client konnte nicht geprüft werden. Bitte erneut installieren.';}
  }
  ipcMain.handle('causalis:invoke', invoke);
  await window.loadURL(UI_URL);
  await browser.initialize();
  browser.layout({sidebarWidth:72,toolbarHeight:110,rightPanelWidth:380});
  await refreshVault();
  window.on('resize',()=>browser.layout({sidebarWidth:72,toolbarHeight:110,rightPanelWidth:vaultOpen ? 380 : 0}));
  window.on('closed',()=>{browser.destroy();window=null;});
  powerMonitor.on('suspend',()=>{++vaultEpoch;vault.lock().catch(()=>{});});
  powerMonitor.on('lock-screen',()=>{++vaultEpoch;vault.lock().catch(()=>{});});
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    {label:'Datei',submenu:[{label:'Neuer Tab',accelerator:'CmdOrCtrl+T',click:()=>browser.createTab()},{label:'Tab schließen',accelerator:'CmdOrCtrl+W',click:()=>browser.closeTab(browser.activeTarget()?.id)},{type:'separator'},{role:'quit',label:'Beenden'}]},
    {label:'Bearbeiten',submenu:[{role:'undo',label:'Rückgängig'},{role:'redo',label:'Wiederholen'},{type:'separator'},{role:'cut',label:'Ausschneiden'},{role:'copy',label:'Kopieren'},{role:'paste',label:'Einfügen'},{role:'selectAll',label:'Alles auswählen'}]},
    {label:'Tresor',submenu:[{label:'Sofort sperren',accelerator:'CmdOrCtrl+Shift+L',click:()=>{++vaultEpoch;vault.lock().then(refreshVault).catch(()=>{});}}]},
    {label:'Ansicht',submenu:[{role:'togglefullscreen',label:'Vollbild'},{label:'Neu laden',accelerator:'CmdOrCtrl+R',click:()=>browser.reload()}]}
  ]));
  if (smoke) {
    const {runSmoke} = require('./smoke.cjs');
    try {
      const report = await runSmoke({app,window,browser,vault,emitState,ipcActions:{invoke},reportDir});
      if (!report.success) throw new Error(report.failure || 'Smoke test failed');
      await vault.lock().catch(()=>{});
      app.exit(0);
    } catch (error) {
      fs.mkdirSync(reportDir,{recursive:true});
      fs.writeFileSync(path.join(reportDir,'fatal.json'),JSON.stringify({passed:false,error:String(error.message || 'Smoke test failed')},null,2));
      console.error('Causalis smoke test failed. See test report.');
      app.exit(1);
    }
  }
}
app.whenReady().then(bootstrap).catch(()=>{console.error('Causalis could not start.');app.exit(1);});
app.on('window-all-closed',()=>app.quit());
app.on('before-quit',(event)=>{
  if (closing || !vault || smoke) return;
  closing=true; ++vaultEpoch; event.preventDefault();
  vault.lock().catch(()=>{}).finally(()=>app.quit());
});
