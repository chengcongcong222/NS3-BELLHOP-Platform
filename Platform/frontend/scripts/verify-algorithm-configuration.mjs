// Real browser + actual Worker catalog. Read-only: never save user drafts.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const [frontend, backend, debug, output] = process.argv.slice(2);
const api = async route => { const response = await fetch(backend + route); assert(response.ok); return response.json(); };
const drafts = await api('/authoring/drafts');
const catalog = await api('/algorithms');
const algorithm = catalog.algorithms.find(a => a.id === 'platform.adaptive-tdma');
assert(algorithm);
const scene = drafts.find(d => d.kind === 'scenario' && d.payload.nodes?.length);
const page = await (await fetch(debug + '/json/new?about:blank', { method: 'PUT' })).json();
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => ws.onopen = resolve);
let id = 0; const pending = new Map();
ws.onmessage = event => { const m = JSON.parse(event.data); if (m.id) { pending.get(m.id)?.(m); pending.delete(m.id); } };
const send = (method, params = {}) => new Promise((resolve, reject) => {
  const n = ++id, timer = setTimeout(() => reject(Error(method)), 20000);
  pending.set(n, m => { clearTimeout(timer); m.error ? reject(Error(JSON.stringify(m.error))) : resolve(m.result); });
  ws.send(JSON.stringify({ id: n, method, params }));
});
const evaluate = async expression => { const r = await send('Runtime.evaluate', { expression, returnByValue: true }); assert(!r.exceptionDetails, JSON.stringify(r.exceptionDetails)); return r.result.value; };
const wait = async expression => { for (let i = 0; i < 180; i++) { if (await evaluate(expression)) return; await new Promise(resolve => setTimeout(resolve, 100)); } throw Error(expression); };
const click = text => evaluate(`(()=>{const element=[...document.querySelectorAll('button, summary')].find(e=>e.textContent.trim()===${JSON.stringify(text)}); if(!element)throw Error('missing control');element.click();})()`);
try {
  fs.mkdirSync(output, { recursive: true });
  await send('Emulation.setDeviceMetricsOverride', { width: 1600, height: 1000, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: frontend + '/workspace/experiment' + (scene ? '?scenario=' + encodeURIComponent(scene.draft_id) : '') });
  const key = `${algorithm.id}@${algorithm.version}@${algorithm.package_sha256}`;
  await wait(`[...document.querySelectorAll('select')].some(e=>e.value===${JSON.stringify(key)})`);
  await click('网络');
  await wait(`!!document.querySelector('.algorithm-parameters')`);
  assert.equal(await evaluate(`document.querySelector('.algorithm-parameters h3').textContent`), algorithm.label);
  await click('算法高级参数');
  const durationLabel = algorithm.parameters.find(p => p.id === 'safety_margin_ns').label;
  await evaluate(`(()=>{const field=[...document.querySelectorAll('.algorithm-parameters label')].find(e=>e.textContent.includes(${JSON.stringify(durationLabel)})).querySelector('input');Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(field,'0.025');field.dispatchEvent(new Event('input',{bubbles:true}));field.dispatchEvent(new Event('change',{bubbles:true}));})()`);
  await wait(`document.querySelector('.algorithm-parameters input').value==='0.025'`);
  assert.equal(await evaluate(`document.querySelectorAll('[role=alert]').length`), 0);
  for (const width of [1600, 1440]) {
    await send('Emulation.setDeviceMetricsOverride', { width, height: width === 1600 ? 1000 : 900, deviceScaleFactor: 1, mobile: false });
    const shot = await send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(path.join(output, `algorithm-settings-${width}.png`), Buffer.from(shot.data, 'base64'));
    assert(await evaluate(`document.documentElement.scrollWidth <= window.innerWidth + 1`));
  }
  assert.deepEqual(await api('/authoring/drafts'), drafts);
  console.log(JSON.stringify({ algorithm: algorithm.id, version: algorithm.version, visibleDurationSeconds: '0.025', userDraftsUnchanged: true, output }));
} finally { ws.close(); await fetch(debug + '/json/close/' + page.id); }
