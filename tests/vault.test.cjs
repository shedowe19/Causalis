'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { VaultBroker, VaultError, secureOrigin, serverAddress } = require('../src/vault.cjs');

const SESSION = 'c2Vzc2lvbi1vbmx5LWluLW1lbW9yeS0xMjM0NQ==';
const GOOD_ID = '11111111-2222-3333-4444-555555555555';
const OTHER_ID = 'aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee';
const loginItem = (overrides = {}) => ({ id: GOOD_ID, name: 'Account', type: 1, reprompt: 0,
  login: { username: 'alice', password: 'site-secret', uris: [{ uri: 'https://example.test/login' }] }, ...overrides });

async function fixture(t, options = {}) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'causalis-vault-test-'));
  const executable = path.join(root, 'bw.exe');
  await fs.writeFile(executable, 'MZfixture');
  const calls = [];
  const states = new Map();
  const invoke = async (executablePath, args, runOptions) => {
    calls.push({ executablePath, args, ...runOptions });
    const key = runOptions.env.BITWARDENCLI_APPDATA_DIR;
    if (!states.has(key)) states.set(key, { logged: false, server: 'https://vault.bitwarden.com' });
    const state = states.get(key);
    if (options.handle) {
      const result = await options.handle(args, runOptions, calls);
      if (result !== undefined) return result;
    }
    let output;
    switch (args[0]) {
      case '--version': output = '2026.9.1\n'; break;
      case 'login': state.logged = true; output = args.includes('--apikey') ? 'Logged in.' : SESSION; break;
      case 'unlock': state.logged = true; output = SESSION; break;
      case 'lock': output = ''; break;
      case 'logout': state.logged = false; output = ''; break;
      case 'status': output = JSON.stringify({ status: state.logged ?
        (runOptions.env.BW_SESSION ? 'unlocked' : 'locked') : 'unauthenticated', serverUrl: state.server }); break;
      case 'config': state.server = args[2]; output = state.server; break;
      case 'sync': output = ''; break;
      case 'list': output = JSON.stringify(options.items || [loginItem()]); break;
      case 'get': output = JSON.stringify(options.getItem || loginItem()); break;
      case 'create': output = JSON.stringify({ ...JSON.parse(Buffer.from(runOptions.input.trim(), 'base64')), id: GOOD_ID }); break;
      default: throw new Error('unexpected command');
    }
    return { code: 0, stdout: output, stderr: '' };
  };
  const broker = new VaultBroker({ executablePath: executable, dataRoot: path.join(root, 'vault'),
    run: invoke, ...options.broker });
  t.after(async () => { broker.dispose(); await fs.rm(root, { recursive: true, force: true }); });
  return { broker, calls, root, executable, states };
}

test('secure server and origin validation rejects unsafe schemes and credentials', () => {
  assert.equal(secureOrigin('https://EXAMPLE.test:443/login?next=x'), 'https://example.test');
  assert.equal(secureOrigin('https://example.test:8443/'), 'https://example.test:8443');
  assert.equal(serverAddress('eu'), 'https://vault.bitwarden.eu');
  assert.equal(serverAddress('https://vault.example.test/base/'), 'https://vault.example.test/base');
  for (const url of ['http://example.test', 'file:///secret', 'javascript:alert(1)',
    'https://alice:secret@example.test', 'https://example.test\0']) {
    assert.throws(() => secureOrigin(url), VaultError);
  }
  for (const url of ['https://example.test/?token=x', 'https://example.test/#secret'])
    assert.throws(() => serverAddress(url), VaultError);
});

test('absent CLI is reported without searching PATH', async t => {
  const { root } = await fixture(t);
  const broker = new VaultBroker({ dataRoot: path.join(root, 'absent'), run: () => assert.fail('must not launch') });
  t.after(() => broker.dispose());
  assert.deepEqual(await broker.status(), { status: 'unavailable', server: 'https://vault.bitwarden.com',
    clientConfigured: false, workspace: 'personal' });
  assert.throws(() => broker.setExecutable('bw.exe'), /official native/);
  await assert.rejects(() => broker.unlock('secret'), { code: 'UNAVAILABLE' });
});

test('explicit executable rejects scripts, symlinks and checksum mismatches', async t => {
  const { broker, root, executable } = await fixture(t);
  const script = path.join(root, 'script', 'bw.exe');
  await fs.mkdir(path.dirname(script));
  await fs.writeFile(script, '#!/bin/sh\n');
  await assert.rejects(() => broker.setExecutable(script), { code: 'INVALID_EXECUTABLE' });
  const linked = path.join(root, 'link', 'bw.exe');
  await fs.mkdir(path.dirname(linked));
  try {
    await fs.symlink(executable, linked);
    await assert.rejects(() => broker.setExecutable(linked), { code: 'INVALID_EXECUTABLE' });
  } catch (error) { if (!['EPERM', 'EACCES'].includes(error.code)) throw error; }
  await assert.rejects(() => broker.setExecutable(executable, '0'.repeat(64)), { code: 'INVALID_EXECUTABLE' });
  const digest = crypto.createHash('sha256').update(await fs.readFile(executable)).digest('hex');
  assert.equal((await broker.setExecutable(executable, digest)).status, 'unauthenticated');
});

test('master password, session and API key stay out of argv and inherited environment', async t => {
  const names = ['BW_SESSION', 'BW_SERVE', 'BW_CLIENTSECRET', 'NODE_OPTIONS', 'ELECTRON_RUN_AS_NODE', 'BITWARDENCLI_DEBUG'];
  const saved = Object.fromEntries(names.map(name => [name, process.env[name]]));
  for (const name of names) process.env[name] = 'inherited-untrusted-secret';
  t.after(() => { for (const name of names) {
    if (saved[name] === undefined) delete process.env[name]; else process.env[name] = saved[name];
  } });
  const { broker, calls } = await fixture(t);
  assert.equal((await broker.login({ email: 'alice@example.test', password: 'master-secret', code: '123456' })).status, 'unlocked');
  const command = calls.find(call => call.args[0] === 'login');
  assert.equal(command.env.CAUSALIS_BW_PASSWORD, 'master-secret');
  assert(!command.args.includes('master-secret'));
  assert.deepEqual(command.args.slice(-4), ['--method', '0', '--code', '123456']);
  assert.equal(command.env.BW_SESSION, undefined);
  for (const name of names) assert.equal(command.env[name], undefined);
  const status = calls.at(-1);
  assert.equal(status.env.BW_SESSION, SESSION);
  assert.equal(status.env.CAUSALIS_BW_PASSWORD, undefined);
  assert(!status.args.includes(SESSION));
  await broker.logout();
  await broker.login({ method: 'apikey', clientId: 'user.id', clientSecret: 'api-secret' });
  const api = calls.filter(call => call.args[0] === 'login').at(-1);
  assert.equal(api.env.BW_CLIENTID, 'user.id');
  assert.equal(api.env.BW_CLIENTSECRET, 'api-secret');
  assert(!api.args.join(' ').includes('api-secret'));
  assert.equal((await broker.status()).status, 'locked');
  assert.equal((await broker.unlock('master-secret')).status, 'unlocked');
});

test('each workspace owns separate CLI state and switching clears unlock selections', async t => {
  const { broker, calls } = await fixture(t);
  await broker.unlock('master');
  await broker.matches('https://example.test');
  await broker.setWorkspace('work');
  assert.equal(broker.isUnlocked(), false);
  assert.equal((await broker.status()).status, 'unauthenticated');
  await assert.rejects(() => broker.credential(GOOD_ID, 'https://example.test'), { code: 'LOCKED' });
  await broker.setWorkspace('personal');
  assert.equal((await broker.status()).status, 'locked');
  await broker.setWorkspace('homelab');
  await broker.setWorkspace('private');
  const dirs = new Set(calls.map(call => path.basename(call.env.BITWARDENCLI_APPDATA_DIR)));
  assert.deepEqual([...dirs].sort(), ['homelab', 'personal', 'private', 'work']);
  for (const command of calls.filter(call => call.args[0] === 'lock')) assert.equal(command.env.BW_SESSION, undefined);
  assert.throws(() => broker.setWorkspace('../../outside'), { code: 'INVALID_INPUT' });
});

test('server configuration requires logout and uses validated HTTPS base', async t => {
  const { broker, calls } = await fixture(t);
  assert.equal((await broker.configure('eu')).server, 'https://vault.bitwarden.eu');
  assert(calls.some(call => call.args[0] === 'config' && call.args[2] === 'https://vault.bitwarden.eu'));
  await broker.unlock('master');
  await assert.rejects(() => broker.configure('https://vault.example.test'), { code: 'LOGOUT_REQUIRED' });
  await broker.logout();
  assert.equal((await broker.configure('https://vault.example.test/base/')).server, 'https://vault.example.test/base');
});

test('matches exposes only metadata and rejects subdomains, ports, never-match and reprompt items', async t => {
  const items = [loginItem(), loginItem({ id: OTHER_ID,
    login: { username: 'other', password: 'private', uris: [{ uri: 'https://evil.example.test' }] } }),
  loginItem({ id: OTHER_ID, login: { uris: [{ uri: 'http://example.test' }] } }),
  loginItem({ id: OTHER_ID, login: { uris: [{ uri: 'https://example.test:8443' }] } }),
  loginItem({ id: OTHER_ID, login: { uris: [{ uri: 'https://example.test', match: 5 }] } }),
  loginItem({ id: OTHER_ID, reprompt: 1 }), loginItem({ id: OTHER_ID, deletedDate: '2026-01-01' }),
  loginItem({ id: OTHER_ID, type: 2 })];
  const { broker } = await fixture(t, { items });
  await broker.unlock('master');
  const matches = await broker.matches('https://example.test/article');
  assert.deepEqual(matches, [{ id: GOOD_ID, name: 'Account', username: 'alice', uri: 'https://example.test' }]);
  assert(!JSON.stringify(matches).includes('site-secret'));
  await assert.rejects(() => broker.credential(GOOD_ID, 'https://other.test'), { code: 'SELECTION_EXPIRED' });
  assert.deepEqual(await broker.credential(GOOD_ID, 'https://example.test'), {
    id: GOOD_ID, username: 'alice', password: 'site-secret', origin: 'https://example.test' });
  await assert.rejects(() => broker.credential(GOOD_ID, 'https://example.test'), { code: 'SELECTION_EXPIRED' });
});

test('selected item must still match when retrieved, with no secret returned after mutation', async t => {
  const { broker } = await fixture(t, { getItem: loginItem({
    login: { username: 'alice', password: 'site-secret', uris: [{ uri: 'https://changed.test' }] } }) });
  await broker.unlock('master');
  await broker.matches('https://example.test');
  await assert.rejects(() => broker.credential(GOOD_ID, 'https://example.test'), { code: 'ORIGIN_MISMATCH' });
});

test('credential selections expire and auto-lock removes in-memory access', async t => {
  let now = 1000;
  let notifications = 0;
  const { broker } = await fixture(t, { broker: { now: () => now,
    autoLockMs: 10000, selectionMs: 100, onChange: () => { ++notifications; } } });
  await broker.unlock('master');
  await broker.matches('https://example.test');
  now += 100;
  await assert.rejects(() => broker.credential(GOOD_ID, 'https://example.test'), { code: 'SELECTION_EXPIRED' });
  now += 10000;
  assert.equal(broker.isUnlocked(), false);
  await assert.rejects(() => broker.matches('https://example.test'), { code: 'LOCKED' });
  await new Promise(resolve => setImmediate(resolve));
  assert(notifications >= 2);
  const timed = await fixture(t, { broker: { autoLockMs: 250 } });
  await timed.broker.unlock('master');
  await new Promise(resolve => setTimeout(resolve, 300));
  assert.equal(timed.broker.isUnlocked(), false);
});

test('creating a login sends base64 JSON only over stdin and returns sanitized metadata', async t => {
  const { broker, calls, root } = await fixture(t);
  await broker.unlock('master');
  const result = await broker.createLogin({ name: 'Saved account', username: 'alice',
    password: 'new-site-secret', origin: 'https://example.test/login' });
  assert.deepEqual(result, { id: GOOD_ID, name: 'Saved account', username: 'alice', uri: 'https://example.test' });
  const create = calls.find(call => call.args[0] === 'create');
  assert.deepEqual(create.args, ['create', 'item', '--nointeraction']);
  const payload = JSON.parse(Buffer.from(create.input.trim(), 'base64'));
  assert.equal(payload.login.password, 'new-site-secret');
  assert.deepEqual(payload.login.uris, [{ uri: 'https://example.test', match: 1 }]);
  assert(!JSON.stringify(create.args).includes('new-site-secret'));
  assert(!JSON.stringify(result).includes('new-site-secret'));
  assert.deepEqual(await fs.readdir(path.join(root, 'vault', 'personal')), []);
});

test('CLI errors and invalid output never expose raw secret-bearing diagnostics', async t => {
  const { broker } = await fixture(t, { handle: args => {
    if (args[0] === 'unlock') return { code: 1, stdout: 'master-secret', stderr: 'BW_SESSION=secret' };
  } });
  await assert.rejects(() => broker.unlock('master-secret'), error => {
    assert.equal(error.code, 'CLI_FAILED');
    assert(!error.message.includes('master-secret'));
    assert(!error.message.includes('BW_SESSION'));
    return true;
  });
  const badJson = await fixture(t, { handle: args => {
    if (args[0] === 'status') return { code: 0, stdout: 'not-json secret', stderr: '' };
  } });
  await assert.rejects(() => badJson.broker.status(), { code: 'INVALID_RESPONSE' });
  const excessive = await fixture(t, { handle: args => {
    if (args[0] === 'status') return { code: 0, stdout: 'x'.repeat(8 * 1024 * 1024 + 1), stderr: '' };
  } });
  await assert.rejects(() => excessive.broker.status(), { code: 'OUTPUT_LIMIT' });
});

test('serialized commands prevent concurrent CLI state writes', async t => {
  let active = 0;
  let peak = 0;
  const { broker } = await fixture(t, { handle: async args => {
    if (args[0] !== 'status') return undefined;
    ++active; peak = Math.max(peak, active);
    await new Promise(resolve => setImmediate(resolve));
    --active;
    return { code: 0, stdout: '{"status":"unauthenticated"}', stderr: '' };
  } });
  await Promise.all([broker.status(), broker.status(), broker.status()]);
  assert.equal(peak, 1);
});

test('locking during an in-flight read invalidates the result even if runner ignores abort', async t => {
  let release;
  let started;
  const ready = new Promise(resolve => { started = resolve; });
  const { broker } = await fixture(t, { handle: async args => {
    if (args[0] === 'get') {
      started();
      await new Promise(resolve => { release = resolve; });
      return { code: 0, stdout: JSON.stringify(loginItem()), stderr: '' };
    }
  } });
  await broker.unlock('master');
  await broker.matches('https://example.test');
  const credential = broker.credential(GOOD_ID, 'https://example.test');
  await ready;
  const locked = broker.lock();
  assert.equal(broker.isUnlocked(), false);
  release();
  await assert.rejects(credential, { code: 'CANCELLED' });
  await locked;
});

test('password generation works without vault access using bounded cryptographic randomness', async t => {
  const { broker } = await fixture(t);
  const passwords = new Set(Array.from({ length: 64 }, () => broker.generate()));
  assert.equal(passwords.size, 64);
  for (const password of passwords) assert.match(password, /^[A-Za-z0-9!@#$%&*\-_=+]{24}$/);
});

test('session and selection expiry during CLI retrieval cannot resurrect access', async t => {
  let now = 1000;
  const { broker } = await fixture(t, { broker: { now: () => now, autoLockMs: 1000, selectionMs: 100 },
    handle: args => { if (args[0] === 'get') now += 101; } });
  await broker.unlock('master');
  await broker.matches('https://example.test');
  await assert.rejects(() => broker.credential(GOOD_ID, 'https://example.test'), { code: 'SELECTION_EXPIRED' });
  const expired = await fixture(t, { broker: { now: () => now, autoLockMs: 1000, selectionMs: 2000 },
    handle: args => { if (args[0] === 'get') now += 1001; } });
  await expired.broker.unlock('master');
  await expired.broker.matches('https://example.test');
  await assert.rejects(() => expired.broker.credential(GOOD_ID, 'https://example.test'), { code: 'LOCKED' });
  assert.equal(expired.broker.isUnlocked(), false);
});

test('locking while directory setup is pending prevents spawning a password-bearing command', async t => {
  const { broker, calls } = await fixture(t);
  const original = fs.mkdir;
  let release;
  let started;
  const ready = new Promise(resolve => { started = resolve; });
  let intercepted = false;
  fs.mkdir = async (...args) => {
    if (!intercepted && String(args[0]).endsWith(path.join('vault', 'personal'))) {
      intercepted = true;
      started();
      await new Promise(resolve => { release = resolve; });
    }
    return original(...args);
  };
  t.after(() => { fs.mkdir = original; });
  const unlocking = broker.unlock('master-secret');
  await ready;
  const locking = broker.lock();
  release();
  await assert.rejects(unlocking, { code: 'CANCELLED' });
  await locking;
  assert.equal(calls.filter(command => command.args[0] === 'unlock').length, 0);
});
