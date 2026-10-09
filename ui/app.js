'use strict';

(() => {
  const byId = (id) => document.getElementById(id);
  const bridge = window.causalis;
  const workspaceNames = { personal: 'Persönlich', work: 'Arbeit', homelab: 'Homelab', private: 'Privat' };
  let state = { tabs: [], activeTabId: null, workspace: 'personal', vaultOpen: false, vault: { status: 'unavailable', server: '', clientConfigured: false }, target: null };
  let authMode = 'password';
  let matches = [];
  let matchRequestKey = '';
  let matchGeneration = 0;
  let noticeTimer;
  let vaultBusy = false;
  let lastRenderedServer = '';

  function icon(name, className = 'icon') {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('class', className);
    svg.setAttribute('aria-hidden', 'true');
    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttribute('href', `#i-${name}`);
    svg.append(use);
    return svg;
  }

  function showNotice(message) {
    if (!message) return;
    byId('notice').textContent = String(message);
    byId('notice').hidden = false;
    clearTimeout(noticeTimer);
    noticeTimer = setTimeout(() => { byId('notice').hidden = true; }, 6500);
  }

  function vaultMessage(message, error = false) {
    byId('vault-message').textContent = String(message || '');
    byId('vault-message').classList.toggle('error', error);
    byId('vault-message').hidden = !message;
  }

  async function invoke(action, payload = {}) {
    if (!bridge || typeof bridge.invoke !== 'function') throw new Error('Die Browser-Verbindung ist noch nicht bereit.');
    const reply = await bridge.invoke(action, payload);
    if (reply && reply.ok === false) throw new Error(typeof reply.error === 'string' ? reply.error : reply.error?.message || 'Die Aktion konnte nicht abgeschlossen werden.');
    const result = reply?.ok === true ? (reply.result ?? reply.value ?? reply.data ?? reply) : reply;
    if (result?.state) updateState(result.state);
    return result;
  }

  async function browserAction(action, payload = {}) {
    try { await invoke(action, payload); }
    catch (error) { showNotice(error.message || 'Die Browser-Aktion ist fehlgeschlagen.'); }
  }

  function clearSecrets() {
    for (const id of ['login-password', 'login-code', 'client-id', 'client-secret', 'unlock-password', 'entry-password']) byId(id).value = '';
  }

  async function vaultAction(action, payload = {}, successMessage = '') {
    if (vaultBusy) return null;
    vaultBusy = true;
    byId('vault-panel').classList.add('busy');
    vaultMessage('');
    try {
      const result = await invoke(action, payload);
      if (successMessage) vaultMessage(successMessage);
      // State events are authoritative; request a snapshot as well after a
      // vault operation so the UI also recovers from a delayed event.
      const snapshot = await invoke('browser:snapshot');
      if (snapshot?.tabs) updateState(snapshot);
      return result;
    } catch (error) {
      vaultMessage(error.message || 'Die Tresor-Aktion ist fehlgeschlagen.', true);
      return null;
    } finally {
      vaultBusy = false;
      byId('vault-panel').classList.remove('busy');
    }
  }

  function activeTab() {
    return state.tabs.find((tab) => tab.id === (state.activeTabId ?? state.activeId)) || state.activeTab || state.tabs[0];
  }

  function safeTarget() {
    const target = state.target;
    if (!target || target.tabId == null || target.epoch == null || !target.origin) return null;
    try {
      const url = new URL(target.origin);
      if (url.protocol !== 'https:' || url.origin !== target.origin || url.username || url.password) return null;
      return { tabId: target.tabId, epoch: target.epoch, origin: url.origin };
    } catch { return null; }
  }

  function renderTabs() {
    const tabs = byId('tabs');
    tabs.replaceChildren();
    const activeId = state.activeTabId ?? state.activeId;
    for (const tab of state.tabs) {
      const row = document.createElement('div');
      row.className = `tab${tab.id === activeId ? ' active' : ''}`;
      row.setAttribute('role', 'tab');
      row.setAttribute('aria-selected', String(tab.id === activeId));
      row.tabIndex = tab.id === activeId ? 0 : -1;
      row.title = String(tab.title || tab.url || 'Neuer Tab');
      row.append(icon(tab.loading ? 'refresh' : 'globe', 'icon tab-icon'));
      const title = document.createElement('span');
      title.className = 'tab-title';
      title.textContent = String(tab.title || 'Neuer Tab');
      row.append(title);
      const close = document.createElement('button');
      close.className = 'tab-close';
      close.setAttribute('aria-label', `Tab schließen: ${title.textContent}`);
      close.title = 'Tab schließen';
      close.append(icon('close'));
      close.addEventListener('click', (event) => { event.stopPropagation(); void browserAction('browser:close-tab', { id: tab.id }); });
      row.append(close);
      row.addEventListener('click', () => { void browserAction('browser:select-tab', { id: tab.id }); });
      row.addEventListener('keydown', (event) => {
        if (event.target !== row) return;
        if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); void browserAction('browser:select-tab', { id: tab.id }); }
        if (event.key === 'ArrowRight' || event.key === 'ArrowLeft') {
          event.preventDefault();
          const index = state.tabs.indexOf(tab);
          const next = state.tabs[(index + (event.key === 'ArrowRight' ? 1 : -1) + state.tabs.length) % state.tabs.length];
          if (next) void browserAction('browser:select-tab', { id: next.id });
        }
      });
      tabs.append(row);
    }
  }

  function renderMatches() {
    const container = byId('matches');
    container.replaceChildren();
    const target = safeTarget();
    if (!target || state.vault.status !== 'unlocked' || !matches.length) {
      const message = document.createElement('p');
      message.className = 'empty-state';
      message.textContent = !target ? 'Öffne eine HTTPS-Webseite, um passende Logins zu sehen.' : 'Für diese Adresse wurde kein passender Login gefunden.';
      container.append(message);
      return;
    }
    for (const item of matches) {
      if (!item || typeof item.id !== 'string') continue;
      const row = document.createElement('div');
      row.className = 'match';
      const avatar = document.createElement('div');
      avatar.className = 'match-avatar';
      avatar.append(icon('key'));
      const copy = document.createElement('div');
      copy.className = 'match-copy';
      const name = document.createElement('div');
      name.className = 'match-name';
      name.textContent = String(item.name || 'Login');
      const username = document.createElement('div');
      username.className = 'match-username';
      username.textContent = String(item.username || 'Kein Benutzername');
      copy.append(name, username);
      const fill = document.createElement('button');
      fill.className = 'match-fill';
      fill.textContent = 'Ausfüllen';
      fill.addEventListener('click', () => {
        const current = safeTarget();
        if (!current || JSON.stringify(current) !== JSON.stringify(target)) { vaultMessage('Die Webseite hat sich geändert. Aktualisiere die passenden Logins.', true); return; }
        void vaultAction('vault:fill', { id: item.id, ...target }, 'Der ausgewählte Login wurde ausgefüllt.');
      });
      row.append(avatar, copy, fill);
      container.append(row);
    }
  }

  async function refreshMatches(force = false) {
    const target = safeTarget();
    const key = target && state.vault.status === 'unlocked' ? JSON.stringify(target) : '';
    if (!key || !state.vaultOpen) {
      if (!key) { ++matchGeneration; matchRequestKey = ''; matches = []; renderMatches(); }
      return;
    }
    if (!force && key === matchRequestKey) return;
    matchRequestKey = key;
    const generation = ++matchGeneration;
    matches = [];
    const loading = document.createElement('p');
    loading.className = 'empty-state';
    loading.textContent = 'Passende Logins werden gesucht …';
    byId('matches').replaceChildren(loading);
    try {
      const result = await invoke('vault:matches');
      if (generation !== matchGeneration || key !== JSON.stringify(safeTarget()) || state.vault.status !== 'unlocked') return;
      matches = Array.isArray(result) ? result : Array.isArray(result?.matches) ? result.matches : [];
      renderMatches();
    } catch (error) {
      if (generation !== matchGeneration) return;
      matchRequestKey = '';
      matches = [];
      renderMatches();
      vaultMessage(error.message || 'Passende Logins konnten nicht geladen werden.', true);
    }
  }

  function renderVault() {
    const vault = state.vault || { status: 'unavailable' };
    const status = vault.status;
    const labels = { unavailable: 'Bitwarden-Client fehlt', unauthenticated: 'Noch nicht angemeldet', locked: 'Tresor gesperrt', unlocked: 'Tresor entsperrt' };
    byId('vault-panel').hidden = !state.vaultOpen;
    byId('vault-toggle').classList.toggle('open', !!state.vaultOpen);
    byId('vault-toggle').setAttribute('aria-expanded', String(!!state.vaultOpen));
    byId('vault-status-label').textContent = labels[status] || 'Status wird geladen';
    for (const id of ['vault-dot', 'vault-status-dot']) byId(id).className = `status-dot ${status === 'unlocked' ? 'unlocked' : status === 'locked' ? 'locked' : ''}`;
    byId('vault-setup').hidden = status === 'unlocked' || status === 'locked';
    byId('vault-login').hidden = status !== 'unauthenticated' || !vault.clientConfigured;
    byId('vault-unlock').hidden = status !== 'locked';
    byId('vault-ready').hidden = status !== 'unlocked';
    byId('cli-hint').textContent = vault.clientConfigured ? 'Bitwarden-Client bereit. Er übernimmt Anmeldung und Verschlüsselung.' : 'Ein Klick lädt den geprüften offiziellen Bitwarden-Client herunter.';
    byId('install-cli').hidden = !!vault.clientConfigured;
    byId('configure-server').disabled = !vault.clientConfigured;
    const server = String(vault.server || 'https://vault.bitwarden.com').replace(/\/$/, '');
    byId('vault-server-label').textContent = server.includes('bitwarden.eu') ? 'Bitwarden EU' : server.includes('bitwarden.com') ? 'Bitwarden US' : (() => { try { return new URL(server).hostname; } catch { return 'Eigener Server'; } })();
    if (server !== lastRenderedServer && document.activeElement !== byId('service') && document.activeElement !== byId('custom-server')) {
      byId('service').value = ['https://vault.bitwarden.com', 'https://vault.bitwarden.eu'].includes(server) ? server : 'custom';
      if (byId('service').value === 'custom') byId('custom-server').value = server;
      byId('custom-server-row').hidden = byId('service').value !== 'custom';
      lastRenderedServer = server;
    }
    const target = safeTarget();
    byId('target-origin').textContent = target?.origin || 'Keine HTTPS-Webseite geöffnet';
    byId('create-submit').disabled = !target;
    byId('refresh-matches').disabled = !target;
    if (status !== 'unlocked') { matches = []; matchRequestKey = ''; ++matchGeneration; }
    if (status === 'unlocked') void refreshMatches();
  }

  function updateState(next) {
    if (!next || typeof next !== 'object') return;
    const previousStatus = state.vault?.status;
    state = { ...state, ...next, vault: { ...state.vault, ...(next.vault || {}) } };
    if (!Array.isArray(state.tabs)) state.tabs = [];
    renderTabs();
    const tab = activeTab();
    if (document.activeElement !== byId('address')) {
      const url = String(tab?.url || '');
      byId('address').value = /^(?:file:|causalis:)/i.test(url) ? '' : url;
    }
    byId('back').disabled = !(tab?.canGoBack ?? state.canGoBack);
    byId('forward').disabled = !(tab?.canGoForward ?? state.canGoForward);
    byId('reload').disabled = !tab;
    byId('loading-line').hidden = !(tab?.loading ?? state.loading);
    const secure = !tab?.loading && !tab?.error && String(tab?.url || '').startsWith('https://');
    byId('site-icon').classList.toggle('secure', secure);
    byId('site-icon').firstElementChild.setAttribute('href', secure ? '#i-lock' : '#i-globe');
    byId('workspace-name').textContent = workspaceNames[state.workspace] || 'Persönlich';
    for (const button of document.querySelectorAll('[data-workspace]')) {
      const selected = button.dataset.workspace === state.workspace;
      button.classList.toggle('selected', selected);
      button.setAttribute('aria-pressed', String(selected));
    }
    renderVault();
    if (previousStatus !== state.vault.status && state.vault.status !== 'unlocked') clearSecrets();
    if (previousStatus === 'unlocked' && state.vault.status !== 'unlocked') vaultMessage('');
    if (next.notice) showNotice(typeof next.notice === 'string' ? next.notice : next.notice.message);
  }

  function setAuthMode(mode) {
    authMode = mode;
    clearSecrets();
    byId('password-login-fields').hidden = mode !== 'password';
    byId('api-login-fields').hidden = mode !== 'api';
    for (const [id, value] of [['password-login-mode', 'password'], ['api-login-mode', 'api']]) {
      byId(id).classList.toggle('active', mode === value);
      byId(id).setAttribute('aria-pressed', String(mode === value));
    }
  }

  const toggleVault = (open) => { if (!open) clearSecrets(); void browserAction('browser:layout', { vaultOpen: open }); };
  byId('vault-toggle').addEventListener('click', () => toggleVault(!state.vaultOpen));
  byId('rail-vault').addEventListener('click', () => toggleVault(!state.vaultOpen));
  byId('vault-close').addEventListener('click', () => toggleVault(false));
  byId('new-tab').addEventListener('click', () => { void browserAction('browser:new-tab'); });
  for (const action of ['back', 'forward', 'reload']) byId(action).addEventListener('click', () => { void browserAction(`browser:${action}`); });
  for (const button of document.querySelectorAll('[data-workspace]')) button.addEventListener('click', () => { void browserAction('browser:workspace', { id: button.dataset.workspace }); });
  byId('address-form').addEventListener('submit', (event) => { event.preventDefault(); const value = byId('address').value.trim(); if (value) { byId('address').blur(); void browserAction('browser:navigate', { value }); } });
  byId('address').addEventListener('focus', (event) => event.target.select());
  byId('service').addEventListener('change', () => { byId('custom-server-row').hidden = byId('service').value !== 'custom'; });
  byId('configure-server').addEventListener('click', () => {
    const server = byId('service').value === 'custom' ? byId('custom-server').value.trim() : byId('service').value;
    if (!server) { vaultMessage('Gib die HTTPS-Adresse deines Vaultwarden-Servers ein.', true); return; }
    clearSecrets();
    void vaultAction('vault:configure', { server }, 'Die Server-Verbindung wurde aktualisiert.');
  });
  byId('select-cli').addEventListener('click', () => { void vaultAction('vault:select-cli'); });
  byId('install-cli').addEventListener('click', () => { void vaultAction('vault:install-cli', {}, 'Der offizielle Bitwarden-Client ist installiert.'); });
  byId('password-login-mode').addEventListener('click', () => setAuthMode('password'));
  byId('api-login-mode').addEventListener('click', () => setAuthMode('api'));
  byId('login-form').addEventListener('submit', (event) => {
    event.preventDefault();
    const payload = authMode === 'api' ? { clientId: byId('client-id').value.trim(), clientSecret: byId('client-secret').value } : { email: byId('login-email').value.trim(), password: byId('login-password').value, code: byId('login-code').value.trim(), method: Number(byId('login-method').value) };
    clearSecrets();
    if (authMode === 'api' ? !payload.clientId || !payload.clientSecret : !payload.email || !payload.password) { vaultMessage('Fülle die Anmeldedaten vollständig aus.', true); return; }
    void vaultAction('vault:login', payload);
  });
  byId('unlock-form').addEventListener('submit', (event) => {
    event.preventDefault();
    const password = byId('unlock-password').value;
    clearSecrets();
    if (!password) { vaultMessage('Gib dein Master-Passwort ein.', true); return; }
    void vaultAction('vault:unlock', { password });
  });
  byId('vault-sync').addEventListener('click', async () => { const result = await vaultAction('vault:sync', {}, 'Dein Tresor wurde synchronisiert.'); if (result !== null) void refreshMatches(true); });
  byId('vault-lock').addEventListener('click', () => { clearSecrets(); void vaultAction('vault:lock'); });
  for (const id of ['vault-logout', 'locked-logout']) byId(id).addEventListener('click', () => { clearSecrets(); void vaultAction('vault:logout'); });
  byId('refresh-matches').addEventListener('click', () => { void refreshMatches(true); });
  byId('generate-password').addEventListener('click', async () => {
    const result = await vaultAction('vault:generate');
    const password = typeof result === 'string' ? result : result?.password;
    if (typeof password === 'string' && state.vault.status === 'unlocked') { byId('entry-password').value = password; vaultMessage('Ein sicheres Passwort wurde erzeugt.'); }
  });
  byId('create-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    const target = safeTarget();
    const payload = { name: byId('entry-name').value.trim(), username: byId('entry-username').value, password: byId('entry-password').value, origin: target?.origin || '' };
    byId('entry-password').value = '';
    if (!target) { vaultMessage('Öffne zuerst die HTTPS-Webseite für diesen Login.', true); return; }
    if (!payload.name || !payload.password) { vaultMessage('Gib einen Namen und ein Passwort für den Login ein.', true); return; }
    const result = await vaultAction('vault:create', payload, 'Der neue Login wurde im Tresor gespeichert.');
    if (result !== null) { byId('entry-name').value = ''; byId('entry-username').value = ''; void refreshMatches(true); }
  });
  document.addEventListener('keydown', (event) => {
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'l') { event.preventDefault(); byId('address').focus(); byId('address').select(); }
    if (event.key === 'Escape' && document.activeElement === byId('address')) { byId('address').blur(); updateState(state); }
  });
  window.addEventListener('beforeunload', clearSecrets);

  if (bridge && typeof bridge.subscribeState === 'function') bridge.subscribeState(updateState);
  if (bridge && typeof bridge.subscribeFocusAddress === 'function') bridge.subscribeFocusAddress(() => { byId('address').focus(); byId('address').select(); });
  void invoke('browser:snapshot').then((snapshot) => { if (snapshot) updateState(snapshot); }).catch((error) => showNotice(error.message));
})();
