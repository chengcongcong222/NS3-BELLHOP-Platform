// Real Chromium + real Backend capture; no request interception or fixtures.
import { mkdir, writeFile } from 'node:fs/promises';
const [port = '9295', ui = 'http://127.0.0.1:4195', api = 'http://127.0.0.1:8095'] = process.argv.slice(2);
const tabs = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
const ws = new WebSocket(tabs.find(tab => tab.type === 'page').webSocketDebuggerUrl);
await new Promise(resolve => ws.addEventListener('open', resolve, { once: true }));
let id = 0; const pending = new Map();
ws.addEventListener('message', event => { const message = JSON.parse(event.data); if (message.id) { const task = pending.get(message.id); pending.delete(message.id); message.error ? task.reject(message.error) : task.resolve(message.result); } });
function call(method, params = {}) { return new Promise((resolve, reject) => { const callId = ++id; pending.set(callId, { resolve, reject }); ws.send(JSON.stringify({ id: callId, method, params })); }); }
const wait = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const evaluate = async expression => { const result = await call('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }); if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails)); return result.result.value; };
await call('Page.enable');
await call('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
const environmentDraft = await (await fetch(`${api}/authoring/drafts`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ kind: 'environment', name: '浅海试验环境建设草稿', payload: { description: 'R4-02 真实服务器草稿', maximum_depth_meters: 200, maximum_range_meters: 10000, center_frequency_hz: 25000, sound_speed_profile: [{ depth_meters: 0, speed_meters_per_second: 1520 }, { depth_meters: 80, speed_meters_per_second: 1492 }, { depth_meters: 200, speed_meters_per_second: 1504 }], bathymetry: [{ range_meters: 0, depth_meters: 110 }, { range_meters: 5000, depth_meters: 140 }, { range_meters: 10000, depth_meters: 125 }], construction_mode: 'BellhopOffline', build_source: 'reference-bellhop-arrivals', bottom: { sound_speed_meters_per_second: 1700, density_grams_per_cubic_centimeter: 1.8, attenuation_db_per_wavelength: 0.5 }, bellhop: { beam_count: 501, minimum_launch_angle_degrees: -30, maximum_launch_angle_degrees: 30, output: 'Arrivals' } } }) })).json();
const output = new URL('./screenshots/', import.meta.url); await mkdir(output, { recursive: true });
async function navigate(path) {
  const expected = JSON.stringify(path);
  await call('Page.navigate', { url: ui + path });
  for (let index = 0; index < 100; index++) {
    await wait(100);
    if (await evaluate(`location.pathname + location.search === ${expected} && document.readyState === "complete" && !document.body.innerText.includes("正在加载…")`)) return;
  }
  throw new Error(`navigation timed out: ${path}`);
}
async function screenshot(name) { await wait(250); const shot = await call('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false }); await writeFile(new URL(`${name}.png`, output), Buffer.from(shot.data, 'base64')); }
await navigate('/environments'); await screenshot('01-environment-catalog');
await navigate(`/workspace/environment?draft=${encodeURIComponent(environmentDraft.draft_id)}`); await screenshot('02-environment-builder');
await evaluate('document.querySelector(".preview-section")?.scrollIntoView()'); await screenshot('03-environment-profile');
await evaluate('document.querySelector(".builder-section:nth-of-type(5)")?.scrollIntoView()'); await screenshot('04-bellhop-parameters');
await navigate('/workspace/scenario'); await screenshot('05-scenario-studio');
await evaluate('document.querySelectorAll(".node-tool-list button")[1]?.click()'); await screenshot('06-scenario-node-inspector');
await navigate('/workspace/experiment'); await screenshot('07-experiment-channel1');
await evaluate(`[...document.querySelectorAll('.channel-choice button')].find(button => button.innerText.includes('Channel2'))?.click()`);
for (let index = 0; index < 20 && !(await evaluate('Boolean(document.querySelector(".waveform-config"))')); index++) await wait(50);
await screenshot('08-experiment-channel2');
await writeFile(new URL('./capture-evidence.json', import.meta.url), JSON.stringify({ browser: (await call('Browser.getVersion')).product, viewport: [1440, 900], backend: api, environment_draft_id: environmentDraft.draft_id, environment_revision: environmentDraft.revision, screenshots: 8, request_interception: false }, null, 2) + '\n');
ws.close();
