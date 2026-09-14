// Real Edge + real Platform backend capture. No interception or fixture data.
import { writeFile } from 'node:fs/promises';

const [cdp = '127.0.0.1:9305', ui = 'http://127.0.0.1:4205', api = 'http://127.0.0.1:8105'] = process.argv.slice(2);
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
  const result = await call('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
  await writeFile(new URL(`./screenshots/${name}.png`, import.meta.url), Buffer.from(result.data, 'base64'));
}

const created = await fetch(`${api}/authoring/drafts`, {
  method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ kind: 'environment', name: 'R4-05 浅海环境视觉审查', payload: {
    description: '真实服务器草稿，仅用于视觉审查', maximum_depth_meters: 200, maximum_range_meters: 10000,
    center_frequency_hz: 25000,
    sound_speed_profile: [{ depth_meters: 0, speed_meters_per_second: 1520 }, { depth_meters: 80, speed_meters_per_second: 1492 }, { depth_meters: 200, speed_meters_per_second: 1504 }],
    bathymetry: [{ range_meters: 0, depth_meters: 110 }, { range_meters: 5000, depth_meters: 140 }, { range_meters: 10000, depth_meters: 125 }],
    construction_mode: 'BellhopOffline', build_source: 'reference-bellhop-arrivals',
    bottom: { sound_speed_meters_per_second: 1700, density_grams_per_cubic_centimeter: 1.8, attenuation_db_per_wavelength: 0.5 },
    bellhop: { beam_count: 501, minimum_launch_angle_degrees: -30, maximum_launch_angle_degrees: 30, output: 'Arrivals' },
  } }),
});
if (!created.ok) throw Error(await created.text());
const draft = await created.json();

await navigate('/'); await screenshot('01-workbench-new');
await navigate(`/workspace/environment?draft=${encodeURIComponent(draft.draft_id)}`); await screenshot('02-environment-new');
await navigate('/workspace/scenario'); await screenshot('03-scenario-new');
await navigate('/workspace/experiment');
await evaluate('document.querySelector("select[aria-label=\\"物理层实现\\"]").value="Channel2Waveform"; document.querySelector("select[aria-label=\\"物理层实现\\"]").dispatchEvent(new Event("change", {bubbles:true}))');
await wait(150); await screenshot('04-experiment-new');

await writeFile(new URL('./capture-evidence.json', import.meta.url), JSON.stringify({
  browser: (await call('Browser.getVersion')).product,
  viewport: [1440, 900], backend: api, request_interception: false,
  environment_draft_id: draft.draft_id,
  old_reference: 'ux-r1 actual legacy capture at the same 1440x900 viewport',
}, null, 2) + '\n');
socket.close();
