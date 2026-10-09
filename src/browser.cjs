'use strict';

const path = require('node:path');
const { randomUUID } = require('node:crypto');
const { buildAutofillScript } = require('./autofill.cjs');

const WORKSPACES = Object.freeze([
  { id: 'personal', name: 'Persönlich', private: false },
  { id: 'work', name: 'Arbeit', private: false },
  { id: 'homelab', name: 'Homelab', private: false },
  { id: 'private', name: 'Privat', private: true }
]);

function isAllowedNetworkURL(value) {
  try {
    const url = new URL(value);
    return ['https:', 'http:'].includes(url.protocol) && Boolean(url.hostname) && !url.username && !url.password;
  } catch { return false; }
}

function normalizeNavigation(value, startUrl = 'causalis://start/') {
  if (typeof value !== 'string' || value.length > 8192) throw new Error('Die Adresse ist ungültig oder zu lang.');
  const text = value.trim();
  if (!text || ['causalis:start', 'about:start', startUrl].includes(text)) return startUrl;
  if (isAllowedNetworkURL(text)) return new URL(text).href;
  if (!/\s/.test(text) && /^(localhost(?::\d+)?(?:\/|$)|(?:[^/]+\.)[^/]+|\[[\da-f:]+\](?::\d+)?(?:\/|$))/i.test(text)) {
    const url = `${/^localhost(?::\d+)?(?:\/|$)/i.test(text) ? 'http' : 'https'}://${text}`;
    if (isAllowedNetworkURL(url)) return new URL(url).href;
  }
  if (/^[a-z][a-z\d+.-]*:/i.test(text)) throw new Error('Dieser Adresstyp wird nicht geöffnet.');
  return `https://duckduckgo.com/?q=${encodeURIComponent(text)}`;
}

class BrowserController {
  constructor({ window, onState = () => {}, partition = 'causalis', smoke = false,
    startUrl = 'causalis://start/', onSession = () => {} }) {
    if (!window || !startUrl.startsWith('causalis://start/')) throw new Error('Invalid browser host.');
    const { WebContentsView, session, dialog, app } = require('electron');
    this.electron = { WebContentsView, session, dialog, app };
    this.window = window;
    this.onState = onState;
    this.startUrl = startUrl;
    this.workspaceId = 'personal';
    this.tabSequence = 0;
    this.destroyed = false;
    this.currentView = null;
    this.dimensions = { sidebarWidth: 72, toolbarHeight: 110, rightPanelWidth: 0 };
    this.downloads = [];
    this.lastLoad = Promise.resolve();
    this.workspaces = WORKSPACES.map((workspace) => {
      const sessionName = workspace.private || smoke ? `${partition}-${workspace.id}-${randomUUID()}` :
        `persist:${partition}-${workspace.id}`;
      const scopedSession = session.fromPartition(sessionName);
      const result = { ...workspace, tabs: [], activeId: null, session: scopedSession };
      this.configureSession(result);
      onSession(scopedSession, workspace.id);
      return result;
    });
    this.resizeListener = () => this.layout();
    window.on('resize', this.resizeListener);
  }

  workspace() { return this.workspaces.find((item) => item.id === this.workspaceId); }
  activeTab() { return this.workspace().tabs.find((item) => item.id === this.workspace().activeId) || null; }
  allowedMainURL(value) { return value === this.startUrl || isAllowedNetworkURL(value); }

  configureSession(workspace) {
    const scopedSession = workspace.session;
    scopedSession.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
    scopedSession.setPermissionCheckHandler(() => false);
    scopedSession.setDevicePermissionHandler(() => false);
    // No website can request local filesystem resources or the privileged UI
    // protocol. Normal HTTP(S), image data/blob URLs and WebSockets remain usable.
    scopedSession.webRequest.onBeforeRequest({ urls: ['<all_urls>'] }, (details, callback) => {
      let allowed = false;
      try {
        const url = new URL(details.url);
        allowed = isAllowedNetworkURL(details.url) ||
          (['ws:', 'wss:'].includes(url.protocol) && !url.username && !url.password) ||
          (details.url === this.startUrl && details.resourceType === 'mainFrame') ||
          (details.resourceType !== 'mainFrame' && ['data:', 'blob:', 'about:'].includes(url.protocol));
      } catch { /* Invalid addresses are blocked. */ }
      callback({ cancel: !allowed });
    });
    scopedSession.on('will-download', (event, item, contents) => {
      const tab = workspace.tabs.find((candidate) => candidate.view.webContents === contents);
      if (!tab || this.destroyed || this.window.isDestroyed()) { event.preventDefault(); return; }
      const name = item.getFilename().replace(/[<>:"/\\|?*\x00-\x1f]/g, '_').slice(0, 180) || 'download';
      const savePath = this.electron.dialog.showSaveDialogSync(this.window, {
        title: 'Download speichern', defaultPath: path.join(this.electron.app.getPath('downloads'), name),
        properties: ['showOverwriteConfirmation', 'dontAddToRecent']
      });
      if (!savePath) { item.cancel(); return; }
      item.setSavePath(savePath);
      const download = { id: randomUUID(), name, received: 0, total: item.getTotalBytes(), state: 'progressing' };
      this.downloads.unshift(download);
      this.downloads.splice(40);
      item.on('updated', (_event, state) => {
        download.state = state; download.received = item.getReceivedBytes(); this.emit();
      });
      item.once('done', (_event, state) => { download.state = state; this.emit(); });
      this.emit();
    });
  }

  async initialize() {
    if (!this.activeTab()) this.createTab();
    await this.lastLoad;
    return this.snapshot();
  }

  createTab(url = this.startUrl) {
    if (this.destroyed) throw new Error('Der Browser ist geschlossen.');
    const destination = normalizeNavigation(url, this.startUrl);
    const workspace = this.workspace();
    if (workspace.tabs.length >= 50) throw new Error('In diesem Arbeitsbereich sind bereits 50 Tabs geöffnet.');
    const view = new this.electron.WebContentsView({
      webPreferences: { session: workspace.session, nodeIntegration: false, nodeIntegrationInWorker: false,
        nodeIntegrationInSubFrames: false, contextIsolation: true, sandbox: true, webSecurity: true,
        allowRunningInsecureContent: false, webviewTag: false, navigateOnDragDrop: false, spellcheck: true }
    });
    const tab = { id: `tab-${++this.tabSequence}`, view, title: 'Neuer Tab', url: destination,
      loading: true, error: '', favicon: '', epoch: 0, workspace: workspace.id };
    workspace.tabs.push(tab);
    this.bindTab(tab);
    this.selectTab(tab.id);
    this.lastLoad = this.load(tab, destination);
    return tab.id;
  }

  bindTab(tab) {
    const contents = tab.view.webContents;
    const guard = (event, legacyUrl) => {
      const url = event.url || legacyUrl;
      const main = event.isMainFrame !== false;
      const permitted = main ? this.allowedMainURL(url) : isAllowedNetworkURL(url) ||
        (() => { try { return ['about:', 'data:', 'blob:'].includes(new URL(url).protocol); } catch { return false; } })();
      if (!permitted) { event.preventDefault(); tab.error = 'Diese Navigation wurde aus Sicherheitsgründen blockiert.'; this.emit(); }
    };
    contents.on('will-navigate', guard);
    contents.on('will-frame-navigate', guard);
    contents.on('will-redirect', guard);
    contents.setWindowOpenHandler(({ url }) => {
      if (isAllowedNetworkURL(url) && tab.workspace === this.workspaceId) {
        try { this.createTab(url); } catch { /* The tab cap can refuse a popup. */ }
      }
      return { action: 'deny' };
    });
    contents.on('will-attach-webview', (event) => event.preventDefault());
    contents.on('did-start-navigation', (details, _url, _inPlace, legacyMain) => {
      if (details.isMainFrame === true || legacyMain === true) { ++tab.epoch; tab.error = ''; this.emit(); }
    });
    contents.on('did-start-loading', () => { tab.loading = true; this.emit(); });
    contents.on('did-stop-loading', () => { tab.loading = false; this.updateTab(tab); });
    contents.on('did-navigate', () => this.updateTab(tab));
    contents.on('did-navigate-in-page', () => this.updateTab(tab));
    contents.on('page-title-updated', (_event, title) => { tab.title = String(title).slice(0, 160); this.emit(); });
    contents.on('page-favicon-updated', (_event, icons) => {
      tab.favicon = icons.find((value) => value.length <= 4096 && isAllowedNetworkURL(value)) || ''; this.emit();
    });
    contents.on('did-fail-load', (_event, code, _description, _url, isMainFrame) => {
      if (isMainFrame && code !== -3) { tab.loading = false; tab.error = `Die Seite konnte nicht geladen werden (${code}).`; this.emit(); }
    });
    contents.on('render-process-gone', () => { ++tab.epoch; tab.loading = false; tab.error = 'Der Seitenprozess wurde beendet. Bitte lade den Tab neu.'; this.emit(); });
    contents.on('before-input-event', (event, input) => this.keyboard(event, input));
  }

  updateTab(tab) {
    if (this.destroyed || tab.view.webContents.isDestroyed()) return;
    tab.url = tab.view.webContents.getURL() || tab.url;
    tab.title = (tab.view.webContents.getTitle() || tab.title).slice(0, 160);
    this.emit();
  }

  async load(tab, url) {
    if (!this.allowedMainURL(url)) throw new Error('Diese Navigation ist nicht erlaubt.');
    ++tab.epoch; tab.error = ''; tab.loading = true; tab.url = url; this.emit();
    try { await tab.view.webContents.loadURL(url); }
    catch (error) {
      if (error.code !== 'ERR_ABORTED' && !tab.view.webContents.isDestroyed()) {
        tab.loading = false; tab.error = 'Die Seite konnte nicht geladen werden.'; this.emit();
      }
    }
  }

  async navigate(text) {
    const url = normalizeNavigation(text, this.startUrl);
    if (!this.activeTab()) this.createTab(url);
    else this.lastLoad = this.load(this.activeTab(), url);
    await this.lastLoad;
    return this.snapshot();
  }

  selectTab(id) {
    const workspace = this.workspace();
    const tab = workspace.tabs.find((item) => item.id === id);
    if (!tab) throw new Error('Dieser Tab gehört nicht zum aktuellen Arbeitsbereich.');
    if (this.currentView && this.currentView !== tab.view) this.window.contentView.removeChildView(this.currentView);
    workspace.activeId = id;
    if (this.currentView !== tab.view) this.window.contentView.addChildView(tab.view);
    this.currentView = tab.view;
    this.layout();
    tab.view.webContents.focus();
    this.emit();
  }

  closeTab(id) {
    const workspace = this.workspace();
    const index = workspace.tabs.findIndex((item) => item.id === id);
    if (index < 0) throw new Error('Dieser Tab ist nicht geöffnet.');
    const [tab] = workspace.tabs.splice(index, 1);
    if (this.currentView === tab.view) { this.window.contentView.removeChildView(tab.view); this.currentView = null; }
    if (!tab.view.webContents.isDestroyed()) tab.view.webContents.close({ waitForBeforeUnload: false });
    if (!workspace.tabs.length) { workspace.activeId = null; this.createTab(); }
    else if (workspace.activeId === id) this.selectTab(workspace.tabs[Math.min(index, workspace.tabs.length - 1)].id);
    else this.emit();
  }

  setWorkspace(id) {
    if (!this.workspaces.some((item) => item.id === id)) throw new Error('Unbekannter Arbeitsbereich.');
    if (this.currentView) { this.window.contentView.removeChildView(this.currentView); this.currentView = null; }
    this.workspaceId = id;
    const workspace = this.workspace();
    if (!workspace.tabs.length) this.createTab();
    else this.selectTab(workspace.activeId || workspace.tabs[0].id);
    return this.snapshot();
  }

  back() { const tab = this.activeTab(); if (tab?.view.webContents.navigationHistory.canGoBack()) tab.view.webContents.navigationHistory.goBack(); }
  forward() { const tab = this.activeTab(); if (tab?.view.webContents.navigationHistory.canGoForward()) tab.view.webContents.navigationHistory.goForward(); }
  reload() { const tab = this.activeTab(); if (tab) { ++tab.epoch; tab.view.webContents.reload(); } }

  layout(options = {}) {
    for (const key of ['sidebarWidth', 'toolbarHeight', 'rightPanelWidth']) {
      if (options[key] !== undefined) {
        if (!Number.isFinite(options[key]) || options[key] < 0 || options[key] > 1000) throw new Error('Ungültige Fensteraufteilung.');
        this.dimensions[key] = Math.round(options[key]);
      }
    }
    if (!this.currentView || this.window.isDestroyed()) return;
    const [width, height] = this.window.getContentSize();
    const x = Math.min(this.dimensions.sidebarWidth, Math.max(0, width - 1));
    const y = Math.min(this.dimensions.toolbarHeight, Math.max(0, height - 1));
    this.currentView.setBounds({ x, y, width: Math.max(1, width - x - this.dimensions.rightPanelWidth), height: Math.max(1, height - y) });
  }

  keyboard(event, input) {
    if (input.type !== 'keyDown') return;
    const command = input.control || input.meta;
    const key = input.key.toLowerCase();
    let handled = true;
    if (command && key === 't') this.createTab();
    else if (command && key === 'w' && this.activeTab()) this.closeTab(this.activeTab().id);
    else if ((command && key === 'r') || key === 'f5') this.reload();
    else if (input.alt && key === 'arrowleft') this.back();
    else if (input.alt && key === 'arrowright') this.forward();
    else if (command && key === 'l') { this.window.webContents.focus(); this.window.webContents.send('browser:focus-address'); }
    else if (command && key === 'tab') {
      const workspace = this.workspace();
      if (workspace.tabs.length) {
        const index = workspace.tabs.findIndex((item) => item.id === workspace.activeId);
        this.selectTab(workspace.tabs[(index + (input.shift ? workspace.tabs.length - 1 : 1)) % workspace.tabs.length].id);
      }
    }
    else handled = false;
    if (handled) event.preventDefault();
  }

  activeTarget() {
    const tab = this.activeTab();
    if (!tab || tab.view.webContents.isDestroyed()) return null;
    const url = tab.view.webContents.getURL();
    let origin = null;
    try { const parsed = new URL(url); if (parsed.protocol === 'https:' && !parsed.username && !parsed.password) origin = parsed.origin; } catch { /* Not a vault target. */ }
    return { id: tab.id, webContents: tab.view.webContents, origin, url, epoch: tab.epoch, workspace: tab.workspace };
  }

  async fillCredential({ id, epoch, origin, username, password }) {
    const target = this.activeTarget();
    if (!target || target.id !== id || target.epoch !== epoch || !origin || target.origin !== origin || target.webContents.isLoadingMainFrame())
      throw new Error('Die Seite hat sich geändert. Bitte wähle den Eintrag erneut.');
    const script = buildAutofillScript({ origin, username, password });
    // executeJavaScriptInIsolatedWorld runs in this WebContents' main frame only.
    const result = await target.webContents.executeJavaScriptInIsolatedWorld(1007, [{ code: script }], true);
    if (!result || result.ok !== true) throw new Error('Kein eindeutiges, sichtbares und sicheres Anmeldeformular gefunden.');
    return { ok: true, fields: result.fields };
  }

  tabState(tab) {
    const contents = tab.view.webContents;
    const history = !contents.isDestroyed() ? contents.navigationHistory : null;
    return { id: tab.id, title: tab.title, url: tab.url === this.startUrl ? 'causalis:start' : tab.url,
      loading: tab.loading, error: tab.error, favicon: tab.favicon, epoch: tab.epoch,
      canGoBack: Boolean(history?.canGoBack()), canGoForward: Boolean(history?.canGoForward()) };
  }

  snapshot() {
    const workspace = this.workspace();
    const tabs = workspace.tabs.map((tab) => this.tabState(tab));
    const activeTab = tabs.find((tab) => tab.id === workspace.activeId) || null;
    return { workspace: workspace.id, workspaceId: workspace.id, tabs, activeId: workspace.activeId,
      activeTabId: workspace.activeId, activeTab, loading: Boolean(activeTab?.loading),
      canGoBack: Boolean(activeTab?.canGoBack), canGoForward: Boolean(activeTab?.canGoForward),
      secure: Boolean(activeTab?.url.startsWith('https://')), downloads: this.downloads.map((item) => ({ ...item })),
      workspaces: this.workspaces.map((item) => ({ id: item.id, name: item.name, private: item.private,
        tabCount: item.tabs.length, activeId: item.activeId })) };
  }

  emit() { if (!this.destroyed) this.onState(this.snapshot()); }

  destroy() {
    if (this.destroyed) return;
    this.destroyed = true;
    this.window.removeListener('resize', this.resizeListener);
    if (this.currentView && !this.window.isDestroyed()) this.window.contentView.removeChildView(this.currentView);
    this.currentView = null;
    for (const workspace of this.workspaces)
      for (const tab of workspace.tabs)
        if (!tab.view.webContents.isDestroyed()) tab.view.webContents.close({ waitForBeforeUnload: false });
  }
}

module.exports = { BrowserController, normalizeNavigation, isAllowedNetworkURL, WORKSPACES };
