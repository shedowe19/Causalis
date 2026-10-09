'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const { EventEmitter } = require('node:events');
const Module = require('node:module');
const { buildAutofillScript } = require('../src/autofill.cjs');
const { BrowserController, normalizeNavigation, isAllowedNetworkURL } = require('../src/browser.cjs');

function fixture({ origin = 'https://login.example', action = '/session', fields, topFrame = true,
  baseURI, submitAction } = {}) {
  const form = { action, submissions: 0, getAttribute(name) { return name === 'action' ? this.action : null; },
    submit() { ++this.submissions; }, requestSubmit() { ++this.submissions; } };
  class Input {
    constructor(options) {
      Object.assign(this, { type: 'text', name: '', id: '', autocomplete: '', disabled: false, readOnly: false,
        hidden: false, isConnected: true, style: {}, width: 180, height: 32, x: 20, y: 20,
        events: [], stored: '', form }, options);
    }
    get value() { return this.stored; }
    set value(value) { this.stored = value; }
    getBoundingClientRect() { return { width: this.width, height: this.height, left: this.x, top: this.y,
      right: this.x + this.width, bottom: this.y + this.height }; }
    dispatchEvent(event) { this.events.push(event.type); this.onEvent?.(event); return true; }
  }
  const inputs = (fields || [{ name: 'username', autocomplete: 'username' }, { type: 'password' }])
    .map((item) => new Input(item));
  form.elements = submitAction === undefined ? inputs : [...inputs,
    { type: 'submit', getAttribute(name) { return name === 'formaction' ? submitAction : null; } }];
  const window = { innerWidth: 1024, innerHeight: 768,
    location: { origin, protocol: new URL(origin).protocol, href: `${origin}/login` },
    getComputedStyle(input) { return { display: 'block', visibility: 'visible', opacity: '1', ...input.style }; } };
  window.top = topFrame ? window : {};
  const context = vm.createContext({ window, document: { baseURI, querySelectorAll() { return inputs; } },
    HTMLInputElement: Input, Event: class { constructor(type, options) { this.type = type; Object.assign(this, options); } }, URL });
  function fill(overrides = {}) {
    const value = vm.runInContext(buildAutofillScript({ origin: 'https://login.example', username: 'alice@example.test',
      password: 'fixture-password', ...overrides }), context, { timeout: 1000 });
    return JSON.parse(JSON.stringify(value));
  }
  return { inputs, form, window, fill };
}

test('fills explicit login fields via native setters and events, without submitting', () => {
  const page = fixture();
  assert.deepEqual(page.fill(), { ok: true, fields: 2 });
  assert.equal(page.inputs[0].value, 'alice@example.test');
  assert.equal(page.inputs[1].value, 'fixture-password');
  assert.deepEqual(page.inputs[0].events, ['input', 'change']);
  assert.deepEqual(page.inputs[1].events, ['input', 'change']);
  assert.equal(page.form.submissions, 0);
});

test('refuses HTTP, different HTTPS origins, ports and subframes before writing', () => {
  for (const options of [{ origin: 'http://login.example' }, { origin: 'https://evil.example' },
    { origin: 'https://login.example:8443' }, { topFrame: false }]) {
    const page = fixture(options);
    assert.equal(page.fill().ok, false);
    assert.ok(page.inputs.every((input) => input.value === '' && input.events.length === 0));
  }
});

test('requires an exact canonical HTTPS origin and bounded credential strings', () => {
  for (const origin of ['http://login.example', 'https://login.example/', 'https://user:secret@login.example',
    'https://login.example/path', 'data:text/html,test', '', 'invalid'])
    assert.throws(() => buildAutofillScript({ origin, username: 'a', password: 'b' }));
  assert.throws(() => buildAutofillScript({ origin: 'https://login.example', password: '' }));
  assert.throws(() => buildAutofillScript({ origin: 'https://login.example', password: 'x'.repeat(16385) }));
});

test('blocks cross-origin, HTTP and credential-bearing form actions', () => {
  for (const action of ['https://evil.example/collect', '//evil.example/collect', 'http://login.example/post',
    'https://user:pass@login.example/post', 'javascript:alert(1)']) {
    const page = fixture({ action });
    assert.equal(page.fill().ok, false);
    assert.equal(page.inputs[1].value, '');
  }
  assert.equal(fixture({ action: '/same-origin-login' }).fill().ok, true);
});

test('checks document base URLs and submit-button action overrides', () => {
  for (const options of [{ baseURI: 'https://evil.example/' },
    { submitAction: 'https://evil.example/collect' }, { submitAction: 'http://login.example/post' }]) {
    const page = fixture(options);
    assert.equal(page.fill().ok, false);
    assert.ok(page.inputs.every((input) => !input.value));
  }
  assert.equal(fixture({ submitAction: '/same-origin' }).fill().ok, true);
});

test('excludes hidden, tiny, disabled, read-only and new-password fields', () => {
  for (const options of [{ hidden: true }, { disabled: true }, { readOnly: true }, { isConnected: false },
    { width: 0 }, { height: 1 }, { style: { opacity: '0' } }, { style: { display: 'none' } },
    { style: { visibility: 'hidden' } }, { autocomplete: 'new-password' }, { autocomplete: 'section-signup new-password' },
    { x: -1000 }, { y: 1000 }, { checkVisibility() { return false; } },
    { matches(selector) { return selector === ':disabled'; } }, { closest() { return {}; } }]) {
    const page = fixture({ fields: [{ name: 'username' }, { type: 'password', ...options }] });
    assert.equal(page.fill().ok, false);
    assert.equal(page.inputs[1].value, '');
  }
});

test('refuses ambiguous multiple password fields', () => {
  const page = fixture({ fields: [{ name: 'username' }, { type: 'password' }, { type: 'password' }] });
  assert.equal(page.fill().reason, 'ambiguous-password-fields');
  assert.ok(page.inputs.every((input) => !input.value));
});

test('does not put a username into unrelated search fields outside a form', () => {
  const page = fixture({ fields: [{ type: 'text', name: 'search', form: null }, { type: 'password', form: null }] });
  assert.deepEqual(page.fill(), { ok: true, fields: 1 });
  assert.equal(page.inputs[0].value, '');
  assert.equal(page.inputs[1].value, 'fixture-password');
});

test('rechecks origin, connection and form action after username input events', () => {
  const navigation = fixture();
  navigation.inputs[0].onEvent = () => { navigation.window.location.origin = 'https://evil.example'; };
  assert.equal(navigation.fill().ok, false);
  assert.equal(navigation.inputs[1].value, '');
  const formChange = fixture();
  formChange.inputs[0].onEvent = () => { formChange.form.action = 'https://evil.example/collect'; };
  assert.equal(formChange.fill().ok, false);
  assert.equal(formChange.inputs[1].value, '');
  const detached = fixture();
  detached.inputs[0].onEvent = () => { detached.inputs[1].isConnected = false; };
  assert.equal(detached.fill().ok, false);
  assert.equal(detached.inputs[1].value, '');
});

test('handles quotes, HTML characters, backslashes and line separators as literal values', () => {
  const page = fixture();
  const username = 'user"\\<script>\u2028name';
  const password = 'pass\'"\\\n</script>\u2029literal';
  assert.equal(page.fill({ username, password }).ok, true);
  assert.equal(page.inputs[0].value, username);
  assert.equal(page.inputs[1].value, password);
});

test('bounds candidate scanning to protect the privileged operation', () => {
  const page = fixture({ fields: Array.from({ length: 2001 }, () => ({ type: 'password' })) });
  assert.equal(page.fill().reason, 'too-many-fields');
});

test('browser navigation allows HTTP(S), search and the start page, blocks privileged schemes', () => {
  assert.equal(normalizeNavigation(''), 'causalis://start/');
  assert.equal(normalizeNavigation('example.com'), 'https://example.com/');
  assert.equal(normalizeNavigation('example.com:8443/path'), 'https://example.com:8443/path');
  assert.equal(normalizeNavigation('localhost:8080/test'), 'http://localhost:8080/test');
  assert.equal(normalizeNavigation('http://localhost:8080/test'), 'http://localhost:8080/test');
  assert.equal(normalizeNavigation('images and browsers'), 'https://duckduckgo.com/?q=images%20and%20browsers');
  for (const value of ['file:///C:/secrets', 'javascript:alert(1)', 'data:text/html,test', 'causalis://ui/index.html',
    'https://user:pass@example.com']) assert.throws(() => normalizeNavigation(value));
  for (const value of ['file:///C:/secrets', 'javascript:alert(1)', 'https://user:pass@example.com'])
    assert.equal(isAllowedNetworkURL(value), false);
});

test('controller isolates workspace sessions, denies privileged resources and rejects stale fill targets', async () => {
  class Contents extends EventEmitter {
    constructor(session) {
      super(); this.session = session; this.url = ''; this.closed = false; this.injections = [];
      this.navigationHistory = { canGoBack: () => false, canGoForward: () => false, goBack() {}, goForward() {} };
    }
    isDestroyed() { return this.closed; }
    getURL() { return this.url; }
    getTitle() { return 'Fixture'; }
    isLoadingMainFrame() { return false; }
    focus() {}
    send() {}
    reload() {}
    setWindowOpenHandler(handler) { this.openHandler = handler; }
    async loadURL(url) {
      this.url = url;
      this.emit('did-start-navigation', { isMainFrame: true, url });
      this.emit('did-stop-loading');
    }
    async executeJavaScriptInIsolatedWorld(world, sources) {
      this.injections.push({ world, sources }); return { ok: true, fields: 2 };
    }
    close() { this.closed = true; }
  }
  class ScopedSession extends EventEmitter {
    constructor(name) { super(); this.name = name; this.webRequest = { onBeforeRequest: (_filter, callback) => { this.request = callback; } }; }
    setPermissionRequestHandler(handler) { this.permission = handler; }
    setPermissionCheckHandler(handler) { this.permissionCheck = handler; }
    setDevicePermissionHandler(handler) { this.devicePermission = handler; }
  }
  const sessions = [];
  const views = [];
  const electron = {
    session: { fromPartition(name) { const result = new ScopedSession(name); sessions.push(result); return result; } },
    WebContentsView: class { constructor({ webPreferences }) { this.preferences = webPreferences; this.webContents = new Contents(webPreferences.session); }
      setBounds(bounds) { this.bounds = bounds; } },
    app: { getPath: () => '/tmp' }, dialog: {}
  };
  const window = new EventEmitter();
  Object.assign(window, { webContents: new Contents(null), isDestroyed: () => false, getContentSize: () => [1440, 900],
    contentView: { addChildView(view) { views.push(view); }, removeChildView(view) { views.splice(views.indexOf(view), 1); } } });
  const originalLoad = Module._load;
  Module._load = function(request, parent, isMain) {
    return request === 'electron' ? electron : originalLoad.call(this, request, parent, isMain);
  };
  let browser;
  try {
    browser = new BrowserController({ window });
    await browser.initialize();
    assert.equal(sessions.length, 4);
    assert.ok(sessions.slice(0, 3).every((session) => session.name.startsWith('persist:')));
    assert.equal(sessions[3].name.startsWith('persist:'), false);
    const permits = (url, resourceType) => {
      let result; sessions[0].request({ url, resourceType }, (value) => { result = !value.cancel; }); return result;
    };
    assert.equal(permits('https://images.example/picture.png', 'image'), true);
    assert.equal(permits('data:image/png;base64,AAAA', 'image'), true);
    assert.equal(permits('blob:https://images.example/fixture', 'image'), true);
    assert.equal(permits('wss://socket.example/live', 'webSocket'), true);
    assert.equal(permits('causalis://start/', 'mainFrame'), true);
    for (const url of ['file:///C:/secret', 'causalis://ui/index.html', 'data:text/html,test'])
      assert.equal(permits(url, 'mainFrame'), false);
    let allowed;
    sessions[0].permission(null, 'camera', (value) => { allowed = value; });
    assert.equal(allowed, false);
    assert.equal(sessions[0].permissionCheck(), false);
    await browser.navigate('https://login.example/login');
    const target = browser.activeTarget();
    assert.equal(views.length, 1);
    assert.equal(views[0].preferences.nodeIntegration, false);
    assert.equal(views[0].preferences.sandbox, true);
    assert.equal(views[0].preferences.preload, undefined);
    browser.layout({ rightPanelWidth: 380 });
    assert.deepEqual(views[0].bounds, { x: 72, y: 110, width: 988, height: 790 });
    const credential = { id: target.id, epoch: target.epoch, origin: target.origin, username: 'fixture', password: 'test-value' };
    assert.equal((await browser.fillCredential(credential)).ok, true);
    assert.equal(target.webContents.injections[0].world, 1007);
    await browser.navigate('https://login.example/other');
    await assert.rejects(browser.fillCredential(credential));
    assert.equal(target.webContents.injections.length, 1);
    browser.setWorkspace('work'); await browser.lastLoad;
    assert.notEqual(browser.activeTarget().webContents.session, target.webContents.session);
    await assert.rejects(browser.fillCredential(credential));
    browser.setWorkspace('personal');
    assert.equal(browser.activeTarget().id, target.id);
    assert.equal(views.length, 1);
    for (let count = 1; count < 50; ++count) browser.createTab();
    await browser.lastLoad;
    const fullTarget = browser.activeTarget();
    let prevented = 0;
    for (let count = 0; count < 3; ++count) {
      assert.doesNotThrow(() => fullTarget.webContents.emit('before-input-event',
        { preventDefault() { ++prevented; } }, { type: 'keyDown', key: 't', control: true }));
    }
    assert.equal(browser.snapshot().tabs.length, 50);
    assert.equal(prevented, 0);
    assert.equal(browser.snapshot().activeTab.error, 'Die Browseraktion konnte nicht ausgeführt werden.');
  } finally {
    browser?.destroy();
    Module._load = originalLoad;
  }
  assert.ok(sessions[3].name.includes('-private-'));
});
