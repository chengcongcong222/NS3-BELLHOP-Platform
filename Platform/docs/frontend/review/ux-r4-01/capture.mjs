// Real browser / real backend capture. No request interception or fixture data.
// node capture.mjs <CDP port> <frontend origin> <backend origin>
import { mkdir, writeFile } from 'node:fs/promises';
const [port = '9294', ui = 'http://127.0.0.1:4194', api = 'http://127.0.0.1:8094'] = process.argv.slice(2);
const tabs = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
await new Promise(r => ws.addEventListener('open', r, { once: true }));
let id = 0; const pending = new Map();
ws.addEventListener('message', e => { const m = JSON.parse(e.data); if (m.id) { const p = pending.get(m.id); pending.delete(m.id); m.error ? p.reject(m.error) : p.resolve(m.result); } });
function call(method, params = {}) { return new Promise((resolve, reject) => { const n = ++id; pending.set(n, { resolve, reject }); ws.send(JSON.stringify({ id: n, method, params })); }); }
const evaluate = async expression => { const r = await call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) throw Error(JSON.stringify(r.exceptionDetails)); return r.result.value; };
const wait = ms => new Promise(r => setTimeout(r, ms));
await call('Page.enable'); await call('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
const experiments = await (await fetch(`${api}/experiments`)).json();
const experiment = experiments.find(e => e.fusion.acceptance_profile === 'Acceptance4Node');
if (!experiment) throw Error('No published Acceptance4Node experiment');
const created = await fetch(`${api}/runs`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ experiment_id: experiment.experiment_id, experiment_version: experiment.version }) });
if (!created.ok) throw Error(await created.text());
const run = await created.json();
let terminal;
for (let i = 0; i < 100; i++) { terminal = await (await fetch(`${api}/runs/${run.run_id}`)).json(); if (terminal.lifecycle === 'Completed' || terminal.lifecycle === 'Failed') break; await wait(100); }
if (terminal.lifecycle !== 'Completed') throw Error(JSON.stringify(terminal));
const result = await (await fetch(`${api}/runs/${run.run_id}/results`)).json();
if (!result.visualization?.positions.length) throw Error('Missing real trajectory projection');
const movingNodes = [...new Set(result.visualization.positions.map(p => p.node_id))].filter(id => {
  const points = result.visualization.positions.filter(p => p.node_id === id);
  const a = points[0], b = points.at(-1);
  return a.x_meters !== b.x_meters || a.y_meters !== b.y_meters || a.z_meters !== b.z_meters;
});
if (!movingNodes.length) throw Error('Expected real trajectory displacement in the published acceptance run');
const out = new URL('./screenshots/', import.meta.url); await mkdir(out, { recursive: true });
async function screenshot(name) { await wait(300); const shot = await call('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false }); await writeFile(new URL(`${name}.png`, out), Buffer.from(shot.data, 'base64')); }
async function navigate(path) { await call('Page.navigate', { url: ui + path }); for (let i = 0; i < 50; i++) { await wait(100); if (await evaluate('document.readyState === "complete" && !document.body.innerText.includes("正在加载…")')) break; } }
await navigate('/'); await screenshot('01-workbench');
await navigate(`/scenarios/${experiment.scenario.scenario_id}/versions/${experiment.scenario.version}`); await screenshot('02-scenario');
await navigate(`/runs/${run.run_id}`); await wait(800); await screenshot('03-run-completed');
await evaluate(`document.querySelector('.event-lane button[aria-label*="开始发送"]')?.click()`); await screenshot('04-transmission-replay');
await evaluate(`document.querySelector('.event-lane button[aria-label*="融合轮次"]')?.click()`); await screenshot('05-fusion-replay');
await navigate(`/results/${run.run_id}`); await screenshot('06-results-overview');
await evaluate(`document.getElementById('result-4').scrollIntoView()`); await screenshot('07-results-fusion');
await evaluate(`document.getElementById('result-2').scrollIntoView()`); await screenshot('08-results-acoustic');
await writeFile(new URL('./capture-evidence.json', import.meta.url), JSON.stringify({ browser: (await call('Browser.getVersion')).product, viewport: [1440, 900], run_id: run.run_id, experiment_id: experiment.experiment_id, lifecycle: terminal.lifecycle, position_samples: result.visualization.positions.length, generated_features: result.visualization.generated_feature_count, fusion_sources: result.visualization.fusion_sources, waveform_records: result.visualization.waveform.length, screenshots: 8 }, null, 2) + '\n');
ws.close(); console.log(`Captured real run ${run.run_id}`);
