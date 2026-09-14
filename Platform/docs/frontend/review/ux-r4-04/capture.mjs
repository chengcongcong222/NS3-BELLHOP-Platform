// Real Edge + real Backend/worker/ns-3 capture. No interception or fixture data.
import { mkdir, writeFile } from 'node:fs/promises';

const [cdp = '127.0.0.1:9297', ui = 'http://127.0.0.1:4197', api = 'http://127.0.0.1:8097'] = process.argv.slice(2);
const wait = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const tabs = await (await fetch(`http://${cdp}/json`)).json();
const page = tabs.find(tab => tab.type === 'page');
if (!page) throw Error('No Edge page available');
const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => socket.addEventListener('open', resolve, { once: true }));
let requestId = 0;
const pending = new Map();
socket.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (!message.id) return;
  const request = pending.get(message.id);
  pending.delete(message.id);
  message.error ? request.reject(message.error) : request.resolve(message.result);
});
function call(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++requestId;
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
  });
}
async function evaluate(expression) {
  const result = await call('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
await call('Page.enable');
await call('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });

const output = new URL('./screenshots/', import.meta.url);
await mkdir(output, { recursive: true });
async function navigate(path) {
  await call('Page.navigate', { url: ui + path });
  for (let index = 0; index < 100; ++index) {
    await wait(100);
    if (await evaluate('document.readyState === "complete" && !document.body.innerText.includes("正在加载…")')) return;
  }
  throw Error(`Page did not settle: ${path}`);
}
async function screenshot(name) {
  await wait(250);
  const image = await call('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
  await writeFile(new URL(`${name}.png`, output), Buffer.from(image.data, 'base64'));
}
async function complete(runId) {
  for (let index = 0; index < 200; ++index) {
    const run = await (await fetch(`${api}/runs/${runId}`)).json();
    if (run.lifecycle === 'Completed') return run;
    if (run.lifecycle === 'Failed') throw Error(JSON.stringify(run));
    await wait(50);
  }
  throw Error(`Run did not complete: ${runId}`);
}
async function clickEvent(text) {
  for (let index = 0; index < 60; ++index) {
    const clicked = await evaluate(`(() => { const button=[...document.querySelectorAll('.event-lane button')].find(item => item.getAttribute('aria-label')?.includes(${JSON.stringify(text)})); if (!button) return false; button.click(); return true; })()`);
    if (clicked) return;
    await wait(100);
  }
  throw Error(`Missing event: ${text}`);
}

const environmentDraftResponse = await fetch(`${api}/authoring/drafts`, {
  method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ kind: 'environment', name: 'R4-04 浅海环境视觉审查', payload: {
    description: '真实服务器草稿，仅用于视觉审查', maximum_depth_meters: 200, maximum_range_meters: 10000,
    center_frequency_hz: 25000,
    sound_speed_profile: [{ depth_meters: 0, speed_meters_per_second: 1520 }, { depth_meters: 80, speed_meters_per_second: 1492 }, { depth_meters: 200, speed_meters_per_second: 1504 }],
    bathymetry: [{ range_meters: 0, depth_meters: 110 }, { range_meters: 5000, depth_meters: 140 }, { range_meters: 10000, depth_meters: 125 }],
    construction_mode: 'BellhopOffline', build_source: 'reference-bellhop-arrivals',
    bottom: { sound_speed_meters_per_second: 1700, density_grams_per_cubic_centimeter: 1.8, attenuation_db_per_wavelength: 0.5 },
    bellhop: { beam_count: 501, minimum_launch_angle_degrees: -30, maximum_launch_angle_degrees: 30, output: 'Arrivals' },
  } }),
});
if (!environmentDraftResponse.ok) throw Error(await environmentDraftResponse.text());
const environmentDraft = await environmentDraftResponse.json();

await navigate('/'); await screenshot('01-workbench');
await navigate(`/workspace/environment?draft=${encodeURIComponent(environmentDraft.draft_id)}`);
await evaluate('document.querySelector(".preview-section")?.scrollIntoView()'); await screenshot('02-environment-builder');
await navigate('/workspace/scenario'); await screenshot('03-scenario-studio');
await navigate('/workspace/experiment');
await evaluate(`[...document.querySelectorAll('.channel-choice button')].find(button => button.innerText.includes('Channel2'))?.click()`);
for (let index = 0; index < 30 && !(await evaluate('Boolean(document.querySelector(".waveform-config"))')); ++index) await wait(50);
await screenshot('04-experiment-channel2');

const experiments = await (await fetch(`${api}/experiments`)).json();
const experiment = experiments.find(item => item.fusion.acceptance_profile === 'Acceptance4Node');
if (!experiment) throw Error('No formal Acceptance4Node experiment');
const createRun = await fetch(`${api}/runs`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ experiment_id: experiment.experiment_id, experiment_version: experiment.version }) });
if (!createRun.ok) throw Error(await createRun.text());
const created = await createRun.json();
const terminal = await complete(created.run_id);
await navigate(`/runs/${created.run_id}`);
await clickEvent('开始发送'); await screenshot('05-runtime-tx-start');
await clickEvent('信号实际到达'); await screenshot('06-runtime-signal-arrival');
await clickEvent('融合轮次'); await screenshot('07-runtime-fusion');
await navigate(`/results/${created.run_id}`); await screenshot('08-results-overview');

await writeFile(new URL('./capture-evidence.json', import.meta.url), JSON.stringify({
  browser: (await call('Browser.getVersion')).product,
  viewport: [1440, 900], backend: api,
  run_id: created.run_id, lifecycle: terminal.lifecycle,
  environment_draft_id: environmentDraft.draft_id,
  screenshots: 8, request_interception: false,
}, null, 2) + '\n');
socket.close();
console.log(`Captured R4-04 from real run ${created.run_id}`);
