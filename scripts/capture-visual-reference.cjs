// Run with the fixture's exact Electron version, not plain Node.js.
const { app, BrowserWindow } = require('electron');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const os = require('node:os');
const assert = require('node:assert/strict');

function option(name, fallback) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  if (!process.argv[index + 1] || process.argv[index + 1].startsWith('--')) throw new Error(`Missing ${name} value`);
  return process.argv[index + 1];
}

app.disableHardwareAcceleration();
app.commandLine.appendSwitch('force-device-scale-factor', '1');
app.on('window-all-closed', () => {});
const deadline = setTimeout(() => { console.error('Visual reference capture exceeded 60 seconds'); app.exit(1); }, 60000);
deadline.unref();
app.whenReady().then(async () => {
  const root = path.resolve(__dirname, '..');
  const fixturePath = path.resolve(option('--config', path.join(root, 'tests/visual/fixture.json')));
  const output = option('--output');
  if (!output) throw new Error('--output is required');
  const directory = path.resolve(output);
  const fixtureBytes = fs.readFileSync(fixturePath);
  const fixture = JSON.parse(fixtureBytes);
  assert.equal(fixture.schemaVersion, 1, 'Unsupported fixture schema');
  assert.equal(process.versions.electron, fixture.electronVersion, 'Reference Electron version must match fixture');
  assert.equal(process.env.TZ, fixture.timezone, 'TZ must match fixture');
  assert.ok(Array.isArray(fixture.pages) && fixture.pages.length > 0, 'Fixture has no pages');
  fs.mkdirSync(directory, { recursive: true });
  const pages = [];
  const ids = new Set();
  for (const page of fixture.pages) {
    assert.ok(['settings', 'message-settings', 'message-list', 'activation', 'rules'].includes(page.id), 'Unknown page');
    assert.ok(!ids.has(page.id), 'Duplicate page');
    ids.add(page.id);
    for (const dimension of [page.width, page.height]) assert.ok(Number.isInteger(dimension) && dimension >= 320 && dimension <= 4096, 'Invalid viewport');
    const window = new BrowserWindow({
      width: page.width, height: page.height, useContentSize: true, show: false, frame: false,
      webPreferences: {
        preload: path.join(root, 'tests/visual/reference-preload.cjs'),
        contextIsolation: true, nodeIntegration: false, sandbox: false, backgroundThrottling: false,
        additionalArguments: [`--purr-visual-fixture=${fixtureBytes.toString('base64')}`]
      }
    });
    try {
      window.webContents.session.webRequest.onBeforeRequest((details, callback) => {
        callback({ cancel: /^https?:/i.test(details.url) });
      });
      await window.loadFile(path.join(root, 'tests/reference/renderer', `${page.id}.html`));
      await window.webContents.executeJavaScript('document.fonts.ready.then(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))))');
      const bindings = await window.webContents.executeJavaScript(`(() => {
        const result = {};
        for (const binding of ${JSON.stringify(page.bindings)}) {
          if (Object.hasOwn(result, binding.key)) throw new Error('Duplicate binding');
          const element = document.querySelector(binding.reference);
          if (!element) throw new Error('Missing reference binding: ' + binding.key);
          let value = element.textContent;
          if (element.matches('input[type=checkbox]')) value = element.checked;
          else if (element.matches('input[type=number]')) value = Number(element.value);
          else if (element.matches('input,textarea,select')) value = element.value;
          const geometryElement = binding.referenceGeometry ? document.querySelector(binding.referenceGeometry) : element;
          if (!geometryElement) throw new Error('Missing reference geometry: ' + binding.key);
          const rect = geometryElement.getBoundingClientRect();
          const style = getComputedStyle(element);
          result[binding.key] = { value, visible: !!(rect.width && rect.height), enabled: !element.disabled,
            rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height },
            font: { family: style.fontFamily, pixelSize: parseFloat(style.fontSize), weight: Number(style.fontWeight), italic: style.fontStyle === 'italic' } };
        }
        return result;
      })()`);
      for (const binding of page.bindings) assert.deepEqual(bindings[binding.key].value, binding.expected, `Unexpected reference field: ${binding.key}`);
      const state = await window.webContents.executeJavaScript(`({width: innerWidth, height: innerHeight, devicePixelRatio,
        bodyHeight: document.body.scrollHeight, text: document.body.innerText, font: getComputedStyle(document.body).fontFamily})`);
      assert.equal(state.width, page.width); assert.equal(state.height, page.height); assert.equal(state.devicePixelRatio, 1);
      const image = `original-${page.id}.png`;
      const screenshot = await window.webContents.capturePage();
      fs.writeFileSync(path.join(directory, image), screenshot.toPNG());
      pages.push({ id: page.id, image, bindings, ...state });
    } finally { window.destroy(); }
  }
  fs.writeFileSync(path.join(directory, 'reference-manifest.json'), JSON.stringify({
    schemaVersion: 1, kind: 'reference', electronVersion: process.versions.electron,
    chromiumVersion: process.versions.chrome, platform: process.platform, os: os.release(), architecture: process.arch,
    timezone: fixture.timezone, devicePixelRatio: 1,
    fixtureSha256: crypto.createHash('sha256').update(fixtureBytes).digest('hex'), pages
  }, null, 2) + '\n');
  clearTimeout(deadline);
  app.quit();
}).catch(error => { clearTimeout(deadline); console.error(error.stack || error); app.exit(1); });
