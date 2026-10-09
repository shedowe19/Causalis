'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');

const WORKSPACES = new Set(['personal', 'work', 'homelab', 'private']);
const OUTPUT_LIMIT = 8 * 1024 * 1024;
const INPUT_LIMIT = 128 * 1024;
const ID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const ALLOWED_ENV = new Set(['SYSTEMROOT', 'WINDIR', 'COMSPEC', 'USERPROFILE',
  'APPDATA', 'LOCALAPPDATA', 'PROGRAMDATA', 'TEMP', 'TMP', 'HOMEDRIVE', 'HOMEPATH',
  'COMPUTERNAME', 'LANG', 'LC_ALL']);

class VaultError extends Error {
  constructor(code, message) { super(message); this.name = 'VaultError'; this.code = code; }
}
function fail(code, message) { throw new VaultError(code, message); }
function bounded(value, name, max = 4096, required = false) {
  if (typeof value !== 'string' || Buffer.byteLength(value) > max || value.includes('\0') ||
      (required && value.length === 0)) fail('INVALID_INPUT', `Invalid ${name}.`);
  return value;
}
function secureOrigin(value) {
  bounded(value, 'site address', 8192, true);
  let url;
  try { url = new URL(value); } catch { fail('INVALID_ORIGIN', 'A valid HTTPS site is required.'); }
  if (url.protocol !== 'https:' || url.username || url.password || !url.hostname)
    fail('INVALID_ORIGIN', 'A valid HTTPS site is required.');
  return url.origin;
}
function serverAddress(value) {
  const presets = { us: 'https://vault.bitwarden.com', eu: 'https://vault.bitwarden.eu' };
  value = presets[value] || value;
  const origin = secureOrigin(value);
  const url = new URL(value);
  if (url.search || url.hash) fail('INVALID_SERVER', 'The vault server cannot contain a query or fragment.');
  // The official CLI appends API paths to this base; preserve self-hosted paths.
  return origin + (url.pathname === '/' ? '' : url.pathname.replace(/\/+$/, ''));
}
function executableAddress(value) {
  bounded(value, 'Bitwarden executable', 32768, true);
  if (!path.isAbsolute(value) || path.basename(value).toLowerCase() !== 'bw.exe')
    fail('INVALID_EXECUTABLE', 'Select the official native bw.exe executable.');
  return path.resolve(value);
}
function metadata(item, origin) {
  return { id: item.id, name: typeof item.name === 'string' ? item.name.slice(0, 256) : 'Login',
    username: typeof item.login?.username === 'string' ? item.login.username.slice(0, 512) : '', uri: origin };
}
function itemMatches(item, origin) {
  if (!item || !ID.test(item.id || '') || item.type !== 1 || item.deletedDate || item.reprompt === 1 ||
      !Array.isArray(item.login?.uris)) return false;
  return item.login.uris.some(entry => {
    if (!entry || typeof entry.uri !== 'string' || entry.match === 4 || entry.match === 5) return false;
    try { return secureOrigin(entry.uri) === origin; } catch { return false; }
  });
}

// No shell, no PATH resolution, no secret arguments, bounded stdout/stderr.
function runCli(executable, args, options) {
  return new Promise((resolve, reject) => {
    let child;
    let done = false;
    let bytes = 0;
    const stdout = [];
    const stderr = [];
    let timer;
    const finish = (error, result) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      options.signal?.removeEventListener('abort', aborted);
      if (error) { child?.kill(); reject(error); } else resolve(result);
    };
    const aborted = () => finish(new VaultError('CANCELLED', 'Vault operation cancelled.'));
    if (options.signal?.aborted) { aborted(); return; }
    try {
      child = spawn(executable, args, { env: options.env, shell: false, windowsHide: true,
        stdio: ['pipe', 'pipe', 'pipe'] });
    } catch { finish(new VaultError('CLI_FAILED', 'Bitwarden could not be started.')); return; }
    options.signal?.addEventListener('abort', aborted, { once: true });
    const consume = chunks => chunk => {
      if (done) return;
      bytes += chunk.length;
      if (bytes > options.maxOutputBytes) {
        finish(new VaultError('OUTPUT_LIMIT', 'Bitwarden output exceeded the allowed size.'));
      } else chunks.push(chunk);
    };
    child.stdout.on('data', consume(stdout));
    child.stderr.on('data', consume(stderr));
    child.on('error', () => finish(new VaultError('CLI_FAILED', 'Bitwarden could not be started.')));
    child.on('close', code => finish(null, { code, stdout: Buffer.concat(stdout).toString('utf8'),
      stderr: Buffer.concat(stderr).toString('utf8') }));
    child.stdin.on('error', () => {});
    child.stdin.end(options.input || '');
    timer = setTimeout(() => finish(new VaultError('TIMEOUT', 'Bitwarden did not finish in time.')), options.timeout);
    timer.unref?.();
  });
}

class VaultBroker {
  constructor({ executablePath = null, dataRoot, run = runCli, expectedSha256 = null,
    now = Date.now, autoLockMs = 5 * 60 * 1000, selectionMs = 60 * 1000,
    onChange = () => {} } = {}) {
    if (typeof dataRoot !== 'string' || !path.isAbsolute(dataRoot))
      fail('INVALID_INPUT', 'An absolute vault data directory is required.');
    this._dataRoot = path.resolve(dataRoot);
    this._executable = executablePath ? executableAddress(executablePath) : null;
    this._expectedHash = expectedSha256;
    this._run = run;
    this._now = now;
    this._autoLockMs = autoLockMs;
    this._selectionMs = selectionMs;
    this._workspace = 'personal';
    this._server = 'https://vault.bitwarden.com';
    this._session = '';
    this._sessionUntil = 0;
    this._selections = new Map();
    this._epoch = 0;
    this._queue = Promise.resolve();
    this._active = null;
    this._timer = null;
    this._onChange = onChange;
  }

  _serial(action) {
    const next = this._queue.then(action);
    this._queue = next.catch(() => {});
    return next;
  }
  _context() { return { epoch: this._epoch, workspace: this._workspace, executable: this._executable }; }
  _assert(context) {
    if (context.epoch !== this._epoch || context.workspace !== this._workspace ||
        context.executable !== this._executable) fail('CANCELLED', 'Vault operation cancelled.');
  }
  _invalidate() {
    ++this._epoch;
    this._session = '';
    this._sessionUntil = 0;
    this._selections.clear();
    clearTimeout(this._timer);
    this._timer = null;
    this._active?.abort();
    this._notify();
  }
  _notify() {
    queueMicrotask(() => {
      try { Promise.resolve(this._onChange()).catch(() => {}); } catch { /* observer cannot weaken locking */ }
    });
  }
  _touch() {
    this._sessionUntil = this._now() + this._autoLockMs;
    clearTimeout(this._timer);
    this._timer = setTimeout(() => { this.lock().catch(() => {}); }, this._autoLockMs);
    this._timer.unref?.();
  }
  _unlocked() {
    if (!this._session || this._now() >= this._sessionUntil) {
      this._invalidate();
      fail('LOCKED', 'Unlock the vault first.');
    }
  }
  _environment(context, extra) {
    const env = {};
    for (const [name, value] of Object.entries(process.env)) {
      if (ALLOWED_ENV.has(name.toUpperCase())) env[name.toUpperCase()] = value;
    }
    env.BITWARDENCLI_APPDATA_DIR = path.join(this._dataRoot, context.workspace);
    // Cleanup commands for a previous context must never receive the current
    // workspace's unlock session, even if more API methods are added later.
    if (this._session && context.epoch === this._epoch && context.workspace === this._workspace &&
        context.executable === this._executable) env.BW_SESSION = this._session;
    return Object.assign(env, extra);
  }
  async _verifyFile(executable) {
    if (!executable) fail('UNAVAILABLE', 'Select the official Bitwarden bw.exe first.');
    let file;
    try {
      const info = await fs.lstat(executable);
      if (!info.isFile() || info.isSymbolicLink() || info.size < 2 || info.size > 256 * 1024 * 1024)
        fail('INVALID_EXECUTABLE', 'Select the official native bw.exe executable.');
      file = await fs.open(executable, 'r');
      const magic = Buffer.alloc(2);
      await file.read(magic, 0, 2, 0);
      if (!magic.equals(Buffer.from('MZ')))
        fail('INVALID_EXECUTABLE', 'Select the official native bw.exe executable.');
      if (this._expectedHash) {
        if (!/^[a-f0-9]{64}$/i.test(this._expectedHash))
          fail('INVALID_EXECUTABLE', 'The Bitwarden checksum is invalid.');
        const hash = crypto.createHash('sha256');
        for await (const chunk of file.createReadStream({ start: 0, autoClose: false })) hash.update(chunk);
        if (hash.digest('hex') !== this._expectedHash.toLowerCase())
          fail('INVALID_EXECUTABLE', 'The Bitwarden executable checksum does not match.');
      }
    } catch (error) {
      if (error instanceof VaultError) throw error;
      fail('UNAVAILABLE', 'The Bitwarden executable is unavailable.');
    } finally { await file?.close(); }
  }
  async _invoke(args, context, { extraEnv = {}, input = '', timeout = 30000, stale = false } = {}) {
    if (!stale) this._assert(context);
    await this._verifyFile(context.executable);
    if (!stale) this._assert(context);
    const directory = path.join(this._dataRoot, context.workspace);
    await fs.mkdir(directory, { recursive: true, mode: 0o700 });
    if (!stale) this._assert(context);
    if (Buffer.byteLength(input) > INPUT_LIMIT) fail('INVALID_INPUT', 'Vault input is too large.');
    const controller = new AbortController();
    this._active = controller;
    let result;
    try {
      result = await this._run(context.executable, args, { env: this._environment(context, extraEnv),
        input, timeout, maxOutputBytes: OUTPUT_LIMIT, signal: controller.signal });
    } catch (error) {
      if (error instanceof VaultError) throw error;
      fail('CLI_FAILED', 'Bitwarden could not complete the operation. Check the server, login and two-step code.');
    } finally { if (this._active === controller) this._active = null; }
    if (!stale) this._assert(context);
    if (!result || result.code !== 0)
      fail('CLI_FAILED', 'Bitwarden could not complete the operation. Check the server, login and two-step code.');
    if (typeof result.stdout !== 'string' || Buffer.byteLength(result.stdout) +
        Buffer.byteLength(typeof result.stderr === 'string' ? result.stderr : '') > OUTPUT_LIMIT)
      fail('OUTPUT_LIMIT', 'Bitwarden output exceeded the allowed size.');
    return result.stdout;
  }
  _json(output) {
    try { return JSON.parse(output); } catch { fail('INVALID_RESPONSE', 'Bitwarden returned an invalid response.'); }
  }
  _setSession(output) {
    const session = output.trim();
    if (!/^[A-Za-z0-9+/=_-]{20,4096}$/.test(session))
      fail('INVALID_RESPONSE', 'Bitwarden did not return a valid unlock session.');
    this._session = session;
    this._touch();
    this._notify();
  }
  async _status(context) {
    if (!context.executable) return { status: 'unavailable', server: this._server,
      clientConfigured: false, workspace: context.workspace };
    const state = this._json(await this._invoke(['status', '--nointeraction'], context));
    if (!state || typeof state !== 'object' || !['unauthenticated', 'locked', 'unlocked'].includes(state.status))
      fail('INVALID_RESPONSE', 'Bitwarden returned an invalid status.');
    if (!this._session || this._now() >= this._sessionUntil) {
      if (this._session) this._invalidate();
      if (state.status === 'unlocked') state.status = 'locked';
    }
    if (typeof state.serverUrl === 'string' && state.serverUrl) {
      try { this._server = serverAddress(state.serverUrl); } catch { /* retain validated setting */ }
    }
    return { status: state.status, server: this._server, clientConfigured: true, workspace: context.workspace };
  }

  status() { const context = this._context(); return this._serial(() => this._status(context)); }
  isUnlocked() {
    if (!this._session) return false;
    if (this._now() >= this._sessionUntil) { this._invalidate(); return false; }
    return true;
  }
  setWorkspace(workspace) {
    if (!WORKSPACES.has(workspace)) fail('INVALID_INPUT', 'Invalid workspace.');
    const previous = this._context();
    this._invalidate();
    this._workspace = workspace;
    this._server = 'https://vault.bitwarden.com';
    const context = this._context();
    return this._serial(async () => {
      if (previous.executable) {
        try { await this._invoke(['lock', '--nointeraction'], previous, { stale: true }); } catch { /* RAM already locked */ }
      }
      return this._status(context);
    });
  }
  setExecutable(executable, expectedSha256 = null) {
    const selected = executableAddress(executable);
    this._invalidate();
    this._executable = selected;
    this._expectedHash = expectedSha256;
    const context = this._context();
    return this._serial(async () => {
      const version = (await this._invoke(['--version'], context)).trim();
      if (!/^\d{4}\.\d{1,2}\.\d{1,3}(?:[-+][a-z0-9.-]+)?$/i.test(version))
        fail('INVALID_EXECUTABLE', 'The selected executable did not identify as Bitwarden CLI.');
      return this._status(context);
    });
  }
  configure(server) {
    const address = serverAddress(server);
    const context = this._context();
    return this._serial(async () => {
      const current = await this._status(context);
      if (current.status !== 'unauthenticated')
        fail('LOGOUT_REQUIRED', 'Log out before changing the vault server.');
      await this._invoke(['config', 'server', address, '--nointeraction'], context);
      this._server = address;
      return this._status(context);
    });
  }
  login({ email = '', password = '', code = '', method = 0, clientId = '', clientSecret = '' } = {}) {
    const context = this._context();
    const apiKey = method === 'apikey' || clientId !== '' || clientSecret !== '';
    if (apiKey) {
      bounded(clientId, 'API client ID', 4096, true);
      bounded(clientSecret, 'API client secret', 4096, true);
      bounded(password, 'master password', 4096);
    } else {
      bounded(email, 'email', 512, true);
      if (email.startsWith('-') || !email.includes('@')) fail('INVALID_INPUT', 'Invalid email.');
      bounded(password, 'master password', 4096, true);
      bounded(code, 'two-step code', 128);
      if (code && !['0', '1', '3'].includes(String(method))) fail('INVALID_INPUT', 'Unsupported two-step method.');
    }
    return this._serial(async () => {
      this._assert(context);
      if (apiKey) {
        await this._invoke(['login', '--apikey', '--nointeraction'], context,
          { extraEnv: { BW_CLIENTID: clientId, BW_CLIENTSECRET: clientSecret }, timeout: 60000 });
        if (password) {
          bounded(password, 'master password', 4096, true);
          this._setSession(await this._invoke(['unlock', '--passwordenv', 'CAUSALIS_BW_PASSWORD', '--raw', '--nointeraction'],
            context, { extraEnv: { CAUSALIS_BW_PASSWORD: password }, timeout: 60000 }));
        }
      } else {
        const args = ['login', email, '--passwordenv', 'CAUSALIS_BW_PASSWORD', '--raw', '--nointeraction'];
        if (code) args.push('--method', String(method), '--code', code);
        this._setSession(await this._invoke(args, context,
          { extraEnv: { CAUSALIS_BW_PASSWORD: password }, timeout: 60000 }));
      }
      return this._status(context);
    });
  }
  unlock(password) {
    bounded(password, 'master password', 4096, true);
    const context = this._context();
    return this._serial(async () => {
      this._setSession(await this._invoke(['unlock', '--passwordenv', 'CAUSALIS_BW_PASSWORD', '--raw', '--nointeraction'],
        context, { extraEnv: { CAUSALIS_BW_PASSWORD: password }, timeout: 60000 }));
      return this._status(context);
    });
  }
  sync() {
    const context = this._context();
    return this._serial(async () => {
      this._unlocked(); this._assert(context);
      await this._invoke(['sync', '--nointeraction'], context, { timeout: 60000 });
      this._unlocked();
      this._selections.clear();
      return this._status(context);
    });
  }
  lock() {
    const context = this._context();
    this._invalidate();
    return this._serial(async () => {
      if (context.executable) {
        try { await this._invoke(['lock', '--nointeraction'], context, { stale: true }); } catch { /* RAM remains locked */ }
      }
      return { status: context.executable ? 'locked' : 'unavailable', server: this._server,
        clientConfigured: !!context.executable, workspace: this._workspace };
    });
  }
  logout() {
    this._invalidate();
    const context = this._context();
    return this._serial(async () => {
      await this._invoke(['logout', '--nointeraction'], context);
      return this._status(context);
    });
  }
  matches(address) {
    const origin = secureOrigin(address);
    const context = this._context();
    return this._serial(async () => {
      this._unlocked(); this._assert(context);
      const items = this._json(await this._invoke(['list', 'items', '--url', origin, '--nointeraction'], context));
      this._unlocked();
      if (!Array.isArray(items) || items.length > 10000) fail('INVALID_RESPONSE', 'Bitwarden returned an invalid item list.');
      const result = items.filter(item => itemMatches(item, origin)).slice(0, 100)
        .map(item => metadata(item, origin));
      this._selections.clear();
      const until = this._now() + this._selectionMs;
      for (const item of result) this._selections.set(item.id, { origin, until });
      return result;
    });
  }
  credential(id, address) {
    if (typeof id !== 'string' || !ID.test(id)) fail('INVALID_INPUT', 'Invalid vault item.');
    const origin = secureOrigin(address);
    const context = this._context();
    return this._serial(async () => {
      this._unlocked(); this._assert(context);
      const selected = this._selections.get(id);
      if (!selected || selected.origin !== origin || this._now() >= selected.until)
        fail('SELECTION_EXPIRED', 'Choose a matching login again.');
      this._selections.delete(id);
      const item = this._json(await this._invoke(['get', 'item', id, '--nointeraction'], context));
      this._unlocked();
      if (this._now() >= selected.until) fail('SELECTION_EXPIRED', 'Choose a matching login again.');
      if (!item || item.id !== id || !itemMatches(item, origin)) fail('ORIGIN_MISMATCH', 'The login no longer matches this site.');
      const username = bounded(item.login.username ?? '', 'username', 4096);
      const password = bounded(item.login.password ?? '', 'password', 16384, true);
      this._touch();
      // Main process only. Never forward this object to the browser chrome IPC.
      return { id, username, password, origin };
    });
  }
  createLogin({ name, username = '', password, origin: address } = {}) {
    bounded(name, 'login name', 256, true);
    bounded(username, 'username', 4096);
    bounded(password, 'password', 16384, true);
    const origin = secureOrigin(address);
    const context = this._context();
    return this._serial(async () => {
      this._unlocked(); this._assert(context);
      const payload = { organizationId: null, collectionIds: [], folderId: null, type: 1,
        name, notes: null, favorite: false, fields: [], reprompt: 0,
        login: { username, password, totp: null, uris: [{ uri: origin, match: 1 }] } };
      // Official `create item` reads base64 JSON from stdin when no payload
      // argument is supplied. Base64 is encoding, not encryption; never persist.
      const input = Buffer.from(JSON.stringify(payload), 'utf8').toString('base64') + '\n';
      const created = this._json(await this._invoke(['create', 'item', '--nointeraction'], context, { input }));
      this._unlocked();
      if (!itemMatches(created, origin)) fail('INVALID_RESPONSE', 'Bitwarden returned an invalid created login.');
      this._selections.clear();
      this._touch();
      return metadata(created, origin);
    });
  }
  generate() {
    const alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%&*-_=+';
    return Array.from({ length: 24 }, () => alphabet[crypto.randomInt(alphabet.length)]).join('');
  }
  dispose() { this._invalidate(); }
}

module.exports = { VaultBroker, VaultError, secureOrigin, serverAddress };
