// Real browser / real backend / real worker capture. No interception or fixtures.
// node capture.mjs <CDP host:port> <frontend origin> <backend origin>
import { mkdir, writeFile } from 'node:fs/promises';

const [cdp = '127.0.0.1:9296', ui = 'http://127.0.0.1:4196', api = 'http://127.0.0.1:8096'] = process.argv.slice(2);
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const tabs = await (await fetch(`http://${cdp}/json`)).json();
const page = tabs.find(tab => tab.type === 'page');
if (!page) throw Error('No browser page available through CDP');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => ws.addEventListener('open', resolve, { once: true }));
let nextId = 0;
const pending = new Map();
ws.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (!message.id) return;
  const request = pending.get(message.id);
  pending.delete(message.id);
  message.error ? request.reject(message.error) : request.resolve(message.result);
});
function call(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++nextId;
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method, params }));
  });
}
async function evaluate(expression) {
  const result = await call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
await call('Page.enable');
await call('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });

async function complete(runId) {
  for (let count = 0; count < 200; ++count) {
    const run = await (await fetch(`${api}/runs/${runId}`)).json();
    if (run.lifecycle === 'Completed') return run;
    if (run.lifecycle === 'Failed') throw Error(JSON.stringify(run));
    await wait(50);
  }
  throw Error(`Run did not complete: ${runId}`);
}
async function events(runId) {
  const body = await (await fetch(`${api}/runs/${runId}/events`)).text();
  return body.split('\n').filter(line => line.startsWith('data: ')).map(line => JSON.parse(line.slice(6)));
}
const experiments = await (await fetch(`${api}/experiments`)).json();
const experiment = experiments.find(item => item.fusion.acceptance_profile === 'Acceptance4Node');
if (!experiment) throw Error('No formal Acceptance4Node experiment');
const created = await fetch(`${api}/runs`, {
  method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ experiment_id: experiment.experiment_id, experiment_version: experiment.version }),
});
if (!created.ok) throw Error(await created.text());
const channel1 = await created.json();
const channel1Terminal = await complete(channel1.run_id);
const channel1Events = await events(channel1.run_id);

const draftResponse = await fetch(`${api}/authoring/drafts`, {
  method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    kind: 'experiment', name: 'R4-03 Channel2 evidence',
    payload: {
      scenario_id: experiment.scenario.scenario_id,
      bit_rate_bits_per_second: experiment.phy.bit_rate_bits_per_second,
      simulation_cycle_count: experiment.simulation_cycle_count,
      deterministic_seed: experiment.deterministic_seed,
      physical_layer: 'Channel2Waveform', waveform_modulation: 'Bpsk',
      waveform_noise: 'None', waveform_noise_psd_db_re_1upa2_per_hz: 0,
    },
  }),
});
if (!draftResponse.ok) throw Error(await draftResponse.text());
const draft = await draftResponse.json();
const channel2Response = await fetch(`${api}/authoring/experiments/${draft.draft_id}/runs`, { method: 'POST' });
if (!channel2Response.ok) throw Error(await channel2Response.text());
const channel2 = await channel2Response.json();
const channel2Terminal = await complete(channel2.run_id);
const channel2Events = await events(channel2.run_id);
const channel2Result = await (await fetch(`${api}/runs/${channel2.run_id}/results`)).json();

function evidenceFor(records) {
  const counts = {};
  const channels = new Map();
  const positions = new Map();
  let exactArrivalCount = 0;
  for (const record of records) {
    const trace = record.trace;
    counts[trace.kind] = (counts[trace.kind] ?? 0) + 1;
    if (trace.kind === 'ChannelOutcome' && trace.payload.outcome.type === 'Signal') {
      channels.set(`${trace.payload.transmission_id}/${trace.payload.receiver_node_id}`, [BigInt(trace.occurred_at_ns), BigInt(trace.payload.outcome.first_arrival_delay_ns)]);
    }
    if (trace.kind === 'NodePosition') {
      const values = positions.get(trace.payload.node_id) ?? [];
      values.push(trace.payload);
      positions.set(trace.payload.node_id, values);
    }
  }
  for (const record of records) if (record.trace.kind === 'SignalArrival') {
    const key = `${record.trace.payload.transmission_id}/${record.trace.payload.receiver_node_id}`;
    const channel = channels.get(key);
    if (channel && BigInt(record.trace.occurred_at_ns) === channel[0] + channel[1]) ++exactArrivalCount;
  }
  const movingNodes = [...positions].filter(([, samples]) => {
    const first = samples[0], last = samples.at(-1);
    return first.x_meters !== last.x_meters || first.y_meters !== last.y_meters || first.z_meters !== last.z_meters;
  }).map(([nodeId]) => nodeId);
  return { counts, exact_arrival_count: exactArrivalCount, moving_nodes: movingNodes };
}
const output = new URL('./screenshots/', import.meta.url);
await mkdir(output, { recursive: true });
async function screenshot(name) {
  await wait(250);
  const image = await call('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
  await writeFile(new URL(`${name}.png`, output), Buffer.from(image.data, 'base64'));
}
async function navigate(path) {
  await call('Page.navigate', { url: ui + path });
  for (let count = 0; count < 80; ++count) {
    await wait(100);
    if (await evaluate('document.readyState === "complete" && !document.body.innerText.includes("正在加载…")')) return;
  }
  throw Error(`Page did not settle: ${path}`);
}
async function clickEvent(text, last = false) {
  const expression = `(() => { const values=[...document.querySelectorAll('.event-lane button')].filter(x => x.getAttribute('aria-label')?.includes(${JSON.stringify(text)})); const button=values[${last ? 'values.length-1' : '0'}]; if(!button) return false; button.click(); return true; })()`;
  if (!await evaluate(expression)) throw Error(`Missing event button: ${text}`);
}

await navigate(`/runs/${channel1.run_id}`);
await clickEvent('开始发送');
await screenshot('01-tx-start');
await clickEvent('信号实际到达');
await screenshot('02-signal-arrival');
await clickEvent('成功送达本节点');
await screenshot('03-decode-success');
await clickEvent('融合轮次');
await screenshot('04-fusion');
await clickEvent('通信周期完成', true);
await evaluate(`document.querySelector('[aria-label="节点 10"]')?.dispatchEvent(new MouseEvent('click', { bubbles: true }))`);
await screenshot('05-trajectory-position');
await evaluate(`(() => { document.querySelector('.studio-node-buttons + button')?.click(); return true; })()`);
await clickEvent('开始发送');
await evaluate(`document.querySelector('.studio-history-controls button:nth-of-type(3)')?.click()`);
await screenshot('06-timeline-single-step');

await navigate(`/runs/${channel2.run_id}`);
await clickEvent('未解码');
await screenshot('07-channel2-decode-failure');
await clickEvent('信号实际到达');
await screenshot('08-channel2-arrival');

const channel1Evidence = evidenceFor(channel1Events);
const channel2Evidence = evidenceFor(channel2Events);
if (channel1Evidence.exact_arrival_count !== channel1Evidence.counts.SignalArrival) throw Error('Channel1 arrival timing mismatch');
if (channel2Evidence.exact_arrival_count !== channel2Evidence.counts.SignalArrival) throw Error('Channel2 arrival timing mismatch');
if (!channel1Evidence.moving_nodes.length || !channel2Evidence.moving_nodes.length) throw Error('Missing trajectory movement evidence');
await writeFile(new URL('./capture-evidence.json', import.meta.url), JSON.stringify({
  browser: (await call('Browser.getVersion')).product,
  viewport: [1440, 900],
  channel1: { run_id: channel1.run_id, lifecycle: channel1Terminal.lifecycle, ...channel1Evidence },
  channel2: {
    run_id: channel2.run_id, lifecycle: channel2Terminal.lifecycle, ...channel2Evidence,
    waveform_records: channel2Result.visualization.waveform.length,
    crc_ok: channel2Result.visualization.waveform.filter(item => item.crc_ok).length,
    crc_failed: channel2Result.visualization.waveform.filter(item => !item.crc_ok).length,
  },
  screenshots: 8,
  request_interception: false,
}, null, 2) + '\n');
ws.close();
console.log(`Captured Channel1 ${channel1.run_id} and Channel2 ${channel2.run_id}`);
