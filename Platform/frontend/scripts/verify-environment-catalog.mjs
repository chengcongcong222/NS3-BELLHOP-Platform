// Real browser, read-only resource browsing; no draft save, publish or Run.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const [frontend, backend, debug, assetId, output] = process.argv.slice(2);
const api = async route => { const response = await fetch(backend + route); assert(response.ok); return response.json(); };
const before = await api('/authoring/drafts?kind=environment');
const summary = (await api('/environments/authoring-summaries'))[assetId];
assert(summary?.available);
const page = await (await fetch(debug + '/json/new?about:blank', { method: 'PUT' })).json();
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => ws.onopen = resolve);
let id = 0; const pending = new Map(); const requests = [];
ws.onmessage = event => { const m = JSON.parse(event.data);
  if (m.id) { pending.get(m.id)?.(m); pending.delete(m.id); }
  if (m.method === 'Network.requestWillBeSent') requests.push(m.params.request.url);
};
const send = (method, params = {}) => new Promise((resolve, reject) => {
  const n = ++id; const timer = setTimeout(() => reject(Error(method)), 20000);
  pending.set(n, m => { clearTimeout(timer); m.error ? reject(Error(JSON.stringify(m.error))) : resolve(m.result); });
  ws.send(JSON.stringify({ id: n, method, params }));
});
const evaluate = async expression => { const result = await send('Runtime.evaluate', { expression, returnByValue: true });
  assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails)); return result.result.value;
};
const wait = async expression => { for (let i = 0; i < 150; i++) { if (await evaluate(expression)) return;
  await new Promise(resolve => setTimeout(resolve, 100)); } throw Error(expression); };
const shot = async name => { const image = await send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync(path.join(output, name), Buffer.from(image.data, 'base64')); };
try {
  fs.mkdirSync(output, { recursive: true });
  await send('Network.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: frontend + '/environments' });
  await wait(`[...document.querySelectorAll('.environment-library-card h2')].some(e => e.textContent === ${JSON.stringify(summary.name)})`);
  assert.equal(requests.filter(url => url.endsWith('/authoring-inputs')).length, 0);
  assert.equal(requests.filter(url => url.endsWith('/authoring-summaries')).length, 1);
  assert.equal(await evaluate("document.querySelectorAll('[role=alert]').length"), 0);
  await shot('environment-catalog.png');
  requests.length = 0;
  await send('Page.navigate', { url: frontend + '/workspace/experiment' });
  await wait(`!![...document.querySelectorAll('select option')].find(e => e.value === ${JSON.stringify(assetId)})`);
  assert.equal(requests.filter(url => url.endsWith('/authoring-inputs')).length, 0);
  await evaluate(`(() => {const option = [...document.querySelectorAll('select option')].find(e => e.value === ${JSON.stringify(assetId)}); const select = option.parentElement; select.value = option.value; select.dispatchEvent(new Event('change', {bubbles:true}));})()`);
  await wait(`document.body.innerText.includes('正在读取场景环境') === false`);
  await wait(`!!document.querySelector('svg[aria-label="可编辑场景画布"]')`);
  await new Promise(resolve => setTimeout(resolve, 500));
  const inputs = requests.filter(url => url.endsWith('/authoring-inputs'));
  assert.deepEqual(inputs, [backend + '/environments/' + encodeURIComponent(assetId) + '/authoring-inputs']);
  assert.equal(await evaluate("document.querySelectorAll('[role=alert]').length"), 0);
  await shot('experiment-selected-environment.png');
  assert.deepEqual(await api('/authoring/drafts?kind=environment'), before);
  console.log(JSON.stringify({ assetId, name: summary.name, catalogInputRequests: 0, catalogSummaryRequests: 1,
    experimentInputRequests: inputs.length, userDraftsUnchanged: true, output }));
} finally { ws.close(); await fetch(debug + '/json/close/' + page.id); }
