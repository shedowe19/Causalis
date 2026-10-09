'use strict';

// This harness is opt-in and is run by the real packaged Electron application.
// Every website and vault credential below is a synthetic test fixture.
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { VaultBroker } = require('./vault.cjs');

const ORIGIN = 'https://smoke.invalid';
const TIMEOUT_MS = 35000;
const TEST_USERNAME = 'causalis-ci-user';
const TEST_PASSWORD = 'Causalis-Smoke-Only!9';
const TEST_SESSION = 'U21va2VUZXN0T25seVNlc3Npb25Ub2tlbg==';
const ITEM_ID = '12345678-1234-4234-8234-123456789012';

function assert(condition, message) {
  if (!condition) throw new Error(message);
}
function safeError(error) {
  return String(error?.message || 'Unknown smoke-test failure.')
    .replaceAll(TEST_PASSWORD, '[test credential]')
    .replaceAll(TEST_SESSION, '[test session]')
    .slice(0, 600);
}
function fixtureHtml(route) {
  const password = route === '/missing' ? '' :
    `<label>Password<input id="password" name="password" type="password" autocomplete="current-password"${route === '/hidden' ? ' style="display:none"' : ''}></label>`;
  const action = route === '/cross-form' ? 'https://cross-origin.invalid/submit' : `${ORIGIN}/submitted`;
  const extraPassword = route === '/ambiguous' ? '<input name="second-password" type="password">' : '';
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Causalis Chromium fixture</title>
<style>
*{box-sizing:border-box}body{margin:0;padding:30px;background:#101b2d;color:#f1f6ff;font:16px system-ui}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.card{background:#213450;padding:20px;border-radius:14px}
.flex{display:flex;align-items:center;gap:12px}h1{margin:0 0 16px;font-size:32px}input{display:block;width:100%;padding:10px;margin:8px 0 14px;border:1px solid #718fb5;border-radius:6px;background:#fff;color:#111}
button{padding:12px 18px;border:0;border-radius:7px;background:#45d6ac;color:#071d19;font-weight:700}img{width:80px;height:40px}canvas{width:80px;height:40px}
</style></head><body><h1 id="heading">Causalis engine check</h1><p id="javascript">Waiting for JavaScript</p>
<main class="grid" id="grid"><section class="card"><div class="flex" id="flex">
<img id="image" src="${ORIGIN}/image.svg" alt="Decoded SVG fixture"><img id="png" src="${ORIGIN}/image.png" alt="Decoded PNG fixture">
<canvas id="canvas" width="40" height="20"></canvas></div><p>Images, CSS Grid, Flexbox and Canvas.</p><button id="button" type="button">Test interaction</button><p id="interaction">Untouched</p></section>
<section class="card"><form id="login" method="post" action="${action}"><label>User<input id="username" name="username" autocomplete="username"></label>${password}${extraPassword}<button type="submit">Sign in</button></form></section></main>
<script>
window.smokeEvents=[];window.smokeSubmits=0;
document.getElementById('javascript').textContent='JavaScript executed';
document.getElementById('button').addEventListener('click',()=>{document.getElementById('interaction').textContent='Clicked';});
document.getElementById('login').addEventListener('submit',event=>{event.preventDefault();window.smokeSubmits++;});
for(const field of document.querySelectorAll('input'))for(const type of ['input','change'])field.addEventListener(type,()=>{window.smokeEvents.push(field.id+':'+type);});
const canvas=document.getElementById('canvas');canvas.getContext('2d').fillStyle='#28c596';canvas.getContext('2d').fillRect(0,0,40,20);
document.documentElement.dataset.ready='true';
</script></body></html>`;
}
function fixtureResponse(request) {
  const url = new URL(request.url);
  if (url.origin !== ORIGIN || request.method !== 'GET')
    return new Response('Smoke fixture request denied.', { status: 403 });
  if (url.pathname === '/image.svg') {
    return new Response('<svg xmlns="http://www.w3.org/2000/svg" width="40" height="20"><rect width="40" height="20" fill="#45d6ac"/><circle cx="20" cy="10" r="7" fill="#101b2d"/></svg>',
      { headers: { 'content-type': 'image/svg+xml', 'cache-control': 'no-store' } });
  }
  if (url.pathname === '/image.png') {
    const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=', 'base64');
    return new Response(png, { headers: { 'content-type': 'image/png', 'cache-control': 'no-store' } });
  }
  if (['/fixture', '/second', '/missing', '/hidden', '/cross-form', '/ambiguous'].includes(url.pathname))
    return new Response(fixtureHtml(url.pathname), { headers: { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' } });
  return new Response('Fixture not found.', { status: 404 });
}
async function loginDatabaseFiles(directory) {
  const found = [];
  const pending = [directory];
  let count = 0;
  while (pending.length) {
    const current = pending.pop();
    const entries = await fs.readdir(current, { withFileTypes: true });
    for (const entry of entries) {
      assert(++count <= 5000, 'Smoke profile file scan exceeded its limit.');
      if (entry.isSymbolicLink()) continue;
      const filename = path.join(current, entry.name);
      if (entry.isDirectory()) pending.push(filename);
      else if (/^Login Data(?:\b|$)/i.test(entry.name)) found.push(filename);
    }
  }
  return found;
}

async function runSmoke({ app, window, browser, vault, emitState = () => {}, ipcActions, reportDir }) {
  assert(process.argv.includes('--smoke-test'), 'Smoke mode must be explicitly enabled.');
  assert(path.isAbsolute(reportDir), 'An absolute smoke-report directory is required.');
  const profile = path.resolve(app.getPath('userData'));
  const relativeProfile = path.relative(path.resolve(os.tmpdir()), profile);
  assert(relativeProfile && !relativeProfile.startsWith(`..${path.sep}`) && relativeProfile !== '..' && !path.isAbsolute(relativeProfile),
    'Smoke tests require a dedicated temporary profile.');
  await fs.mkdir(reportDir, { recursive: true });
  const report = { schema: 1, success: false, startedAt: new Date().toISOString(),
    runtime: { electron: process.versions.electron, chromium: process.versions.chrome, node: process.versions.node },
    tests: [], screenshots: [], network: 'Only session-scoped synthetic HTTPS fixtures; certificate verification remains enabled.' };
  const sessions = new Set();
  let fakeVault;
  let fixtureDirectory;
  let timedOut = false;
  let timer;
  const active = () => {
    assert(!timedOut, 'Smoke test exceeded its 35-second time limit.');
    const target = browser.activeTarget();
    assert(target?.webContents && !target.webContents.isDestroyed(), 'No active browser WebContents.');
    return target;
  };
  const finishLoad = async () => {
    if (browser.lastLoad && typeof browser.lastLoad.then === 'function') await browser.lastLoad;
  };
  const prepareSession = async () => {
    const session = active().webContents.session;
    if (!sessions.has(session)) {
      await session.protocol.handle('https', fixtureResponse);
      sessions.add(session);
    }
    return session;
  };
  const navigate = async route => {
    await prepareSession();
    await browser.navigate(`${ORIGIN}${route}`);
    await finishLoad();
    return active();
  };
  const test = async (name, body) => {
    assert(!timedOut, 'Smoke test exceeded its 35-second time limit.');
    const started = Date.now();
    const row = { name, passed: false };
    report.tests.push(row);
    try { await body(); row.passed = true; }
    catch (error) { row.error = safeError(error); throw error; }
    finally { row.durationMs = Date.now() - started; }
  };
  const screenshot = async (contents, filename) => {
    // Electron 44 rejects capturePage before the first compositor surface
    // exists, even when stayHidden is set. Wait for actual presented frames.
    await contents.executeJavaScript('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
    const image = await contents.capturePage(undefined, { stayHidden: true });
    assert(!image.isEmpty(), 'Chromium returned an empty screenshot.');
    await fs.writeFile(path.join(reportDir, filename), image.toPNG());
    report.screenshots.push(filename);
  };
  const rejected = async action => {
    try { const result = await action(); return result?.ok === false; }
    catch { return true; }
  };
  const fill = async credential => {
    const target = active();
    return browser.fillCredential({ id: target.id, epoch: target.epoch, origin: ORIGIN,
      username: credential.username, password: credential.password });
  };
  const work = async () => {
    // A never-shown BrowserWindow can execute DOM scripts without producing
    // a capturable display surface. This is a smoke-only temporary profile.
    window.showInactive();
    browser.setWorkspace('personal');
    await finishLoad();
    await navigate('/fixture');
    const original = active();
    const originalSession = original.webContents.session;

    await test('Remote renderer is sandboxed and has no privileged preload', async () => {
      assert(!['no-sandbox', 'disable-web-security', 'ignore-certificate-errors', 'allow-insecure-localhost']
        .some(switchName => app.commandLine.hasSwitch(switchName)), 'Global Chromium security bypasses are enabled.');
      const preferences = original.webContents.getLastWebPreferences();
      assert(preferences.sandbox === true && preferences.contextIsolation === true && preferences.nodeIntegration === false,
        'Remote renderer security settings are incorrect.');
      assert(!preferences.preload && !preferences.nodeIntegrationInSubFrames && !preferences.nodeIntegrationInWorker,
        'Remote renderer has a privileged preload or Node integration.');
      const result = await original.webContents.executeJavaScript(`({
        bridge:typeof window.causalis,process:typeof process,require:typeof require,ready:document.documentElement.dataset.ready
      })`);
      assert(result.bridge === 'undefined' && result.process === 'undefined' && result.require === 'undefined' && result.ready === 'true',
        'The website can access privileged APIs or did not execute JavaScript.');
    });
    await test('Chromium renders CSS Grid, Flexbox, SVG, PNG, Canvas and interactive JavaScript', async () => {
      const state = await original.webContents.executeJavaScript(`(async()=>{
        await Promise.all(Array.from(document.images, image=>image.decode()));
        document.getElementById('button').click();
        return {grid:getComputedStyle(document.getElementById('grid')).display,
          flex:getComputedStyle(document.getElementById('flex')).display,
          image:document.getElementById('image').naturalWidth,png:document.getElementById('png').naturalWidth,
          javascript:document.getElementById('javascript').textContent,
          clicked:document.getElementById('interaction').textContent,
          pixel:Array.from(document.getElementById('canvas').getContext('2d').getImageData(1,1,1,1).data)};
      })()`);
      assert(state.grid === 'grid' && state.flex === 'flex' && state.image === 40 && state.png === 1 &&
        state.javascript === 'JavaScript executed' && state.clicked === 'Clicked' &&
        state.pixel.join(',') === '40,197,150,255', 'The real Chromium render/interaction fixture failed.');
      await screenshot(original.webContents, 'page.png');
    });
    await test('Browser chrome uses its actual isolated IPC bridge', async () => {
      const state = await window.webContents.executeJavaScript(`window.causalis.invoke('browser:snapshot')`);
      assert(state && Array.isArray(state.tabs) && state.tabs.length > 0, 'Browser chrome could not retrieve its state through IPC.');
      const before = browser.snapshot().tabs.length;
      await window.webContents.executeJavaScript(`window.causalis.invoke('browser:new-tab',{url:${JSON.stringify(`${ORIGIN}/second`)}})`);
      await finishLoad();
      assert(browser.snapshot().tabs.length === before + 1 && active().id !== original.id,
        'Browser chrome could not create a tab through IPC.');
      assert(original.webContents.getURL() === `${ORIGIN}/fixture`, 'Creating a tab changed the original tab.');
      browser.selectTab(original.id);
      emitState();
    });
    await test('Website WebFrame cannot invoke privileged main IPC', async () => {
      assert(typeof ipcActions?.invoke === 'function', 'The real main IPC handler is unavailable to the smoke harness.');
      const target = active();
      assert(await rejected(() => ipcActions.invoke({ sender: target.webContents, senderFrame: target.webContents.mainFrame }, 'browser:snapshot', {})),
        'An untrusted website frame was allowed to invoke main IPC.');
      assert(await rejected(() => ipcActions.invoke({ sender: window.webContents, senderFrame: target.webContents.mainFrame }, 'browser:snapshot', {})),
        'An untrusted frame was allowed to impersonate browser chrome IPC.');
    });
    await test('Native browser history returns to the previous document', async () => {
      await navigate('/second');
      const contents = active().webContents;
      assert(contents.navigationHistory.canGoBack(), 'Browser history did not retain the previous page.');
      await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => { cleanup(); reject(new Error('History navigation did not finish.')); }, 5000);
        const loaded = () => { cleanup(); resolve(); };
        const failed = (_event, code) => { if (code !== -3) { cleanup(); reject(new Error('History navigation failed.')); } };
        const cleanup = () => { clearTimeout(timeout); contents.removeListener('did-finish-load', loaded); contents.removeListener('did-fail-load', failed); };
        contents.on('did-finish-load', loaded); contents.on('did-fail-load', failed);
        contents.navigationHistory.goBack();
      });
      assert(contents.getURL() === `${ORIGIN}/fixture`, 'Native history navigated to the wrong document.');
    });
    await test('Workspace sessions isolate cookies', async () => {
      await originalSession.cookies.set({ url: ORIGIN, name: 'causalis_ci_workspace', value: 'personal', secure: true });
      browser.setWorkspace('work');
      await finishLoad();
      await navigate('/fixture');
      const workSession = active().webContents.session;
      assert(workSession !== originalSession && (await workSession.cookies.get({ name: 'causalis_ci_workspace' })).length === 0,
        'The work workspace shares personal session data.');
      await workSession.cookies.set({ url: ORIGIN, name: 'causalis_ci_workspace', value: 'work', secure: true });
      browser.setWorkspace('private');
      await finishLoad();
      await navigate('/fixture');
      const privateSession = active().webContents.session;
      assert(privateSession !== originalSession && privateSession !== workSession &&
        (await privateSession.cookies.get({ name: 'causalis_ci_workspace' })).length === 0,
        'The private workspace shares persistent workspace cookies.');
      browser.setWorkspace('personal');
      await finishLoad();
      browser.selectTab(original.id);
      const cookies = await originalSession.cookies.get({ name: 'causalis_ci_workspace' });
      assert(cookies.length === 1 && cookies[0].value === 'personal', 'Personal cookies were lost or overwritten by another workspace.');
      await navigate('/fixture');
    });
    await test('Actual official Bitwarden CLI reports an isolated unauthenticated profile', async () => {
      assert(vault && typeof vault.status === 'function', 'The native Bitwarden broker is unavailable.');
      const state = await vault.status();
      assert(state.clientConfigured === true && state.status === 'unauthenticated',
        'The native Bitwarden CLI did not report a fresh unauthenticated test profile.');
    });
    await test('VaultBroker supplies a selected exact-origin synthetic credential', async () => {
      fixtureDirectory = await fs.mkdtemp(path.join(os.tmpdir(), 'causalis-vault-fixture-'));
      const executablePath = path.join(fixtureDirectory, 'bw.exe');
      await fs.writeFile(executablePath, Buffer.from('MZsynthetic-smoke-fixture'));
      const item = { id: ITEM_ID, type: 1, name: 'Synthetic smoke fixture', reprompt: 0,
        login: { username: TEST_USERNAME, password: TEST_PASSWORD, uris: [{ uri: ORIGIN, match: 1 }] } };
      fakeVault = new VaultBroker({ executablePath, dataRoot: path.join(fixtureDirectory, 'data'), run: async (_executable, args) => {
        let stdout;
        if (args[0] === 'unlock') stdout = TEST_SESSION;
        else if (args[0] === 'status') stdout = JSON.stringify({ status: 'unlocked', serverUrl: 'https://vault.bitwarden.com' });
        else if (args[0] === 'list') stdout = JSON.stringify([item]);
        else if (args[0] === 'get') stdout = JSON.stringify(item);
        else if (args[0] === 'lock') stdout = '';
        else throw new Error('Unexpected synthetic CLI command.');
        return { code: 0, stdout, stderr: '' };
      } });
      await fakeVault.unlock('synthetic-master-password');
      const matches = await fakeVault.matches(ORIGIN);
      assert(matches.length === 1 && matches[0].id === ITEM_ID && !Object.hasOwn(matches[0], 'password'),
        'Vault matching did not return secret-free exact-origin metadata.');
      const credential = await fakeVault.credential(ITEM_ID, ORIGIN);
      const result = await fill(credential);
      assert(result?.ok === true && result.fields === 2, 'Browser autofill did not fill the selected credential.');
    });
    await test('Autofill sets native field values and DOM events without submitting', async () => {
      const state = await active().webContents.executeJavaScript(`({
        username:document.getElementById('username').value===${JSON.stringify(TEST_USERNAME)},
        password:document.getElementById('password').value===${JSON.stringify(TEST_PASSWORD)},
        submits:window.smokeSubmits,events:window.smokeEvents,
        bridge:typeof window.causalis,session:typeof window.BW_SESSION
      })`);
      assert(state.username && state.password && state.submits === 0 && state.bridge === 'undefined' && state.session === 'undefined' &&
        ['username:input', 'username:change', 'password:input', 'password:change'].every(event => state.events.includes(event)),
        'Autofill values, events, API isolation or no-submit behavior failed.');
    });
    for (const [route, label] of [['/missing', 'missing password field'], ['/hidden', 'hidden password field'],
      ['/cross-form', 'cross-origin form'], ['/ambiguous', 'ambiguous password fields']]) {
      await test(`Autofill rejects ${label}`, async () => {
        await navigate(route);
        assert(await rejected(() => fill({ username: TEST_USERNAME, password: TEST_PASSWORD })),
          `Autofill accepted a ${label}.`);
        const state = await active().webContents.executeJavaScript(`({
          filled:Array.from(document.querySelectorAll('input')).some(input=>input.value.length>0),submits:window.smokeSubmits
        })`);
        assert(!state.filled && state.submits === 0, `Rejected ${label} received a credential or was submitted.`);
      });
    }
    await test('Autofill rejects mismatched origins and stale tab epochs', async () => {
      await navigate('/fixture');
      const target = active();
      assert(await rejected(() => browser.fillCredential({ id: target.id, epoch: target.epoch,
        origin: 'https://cross-origin.invalid', username: TEST_USERNAME, password: TEST_PASSWORD })),
        'Autofill accepted a mismatched origin.');
      assert(await rejected(() => browser.fillCredential({ id: target.id, epoch: target.epoch - 1,
        origin: ORIGIN, username: TEST_USERNAME, password: TEST_PASSWORD })),
        'Autofill accepted an obsolete navigation epoch.');
    });
    await test('Chrome password databases are absent from the fresh smoke profile', async () => {
      const files = await loginDatabaseFiles(profile);
      assert(files.length === 0, 'A Chrome Login Data password database was created.');
    });
    emitState();
    await test('Actual browser chrome screenshot is available', async () => {
      await screenshot(window.webContents, 'ui.png');
    });
  };
  try {
    await Promise.race([work(), new Promise((_, reject) => {
      timer = setTimeout(() => { timedOut = true; reject(new Error('Smoke test exceeded its 35-second time limit.')); }, TIMEOUT_MS);
    })]);
    report.success = true;
  } catch (error) {
    report.failure = safeError(error);
  } finally {
    clearTimeout(timer);
    fakeVault?.dispose();
    for (const session of sessions) {
      try { await session.protocol.unhandle('https'); } catch { /* application exits after reporting */ }
    }
    if (fixtureDirectory) await fs.rm(fixtureDirectory, { recursive: true, force: true });
    report.finishedAt = new Date().toISOString();
    await fs.writeFile(path.join(reportDir, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  }
  return report;
}

module.exports = { runSmoke };
